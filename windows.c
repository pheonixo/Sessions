#include <gtk/gtk.h>

/*
 * Copyright (c) 2021, Dec 13 Steven Abner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

/* Terminology:
 *   session: A thread-like part of an application represented by a window.
 *            Used to differentate between drawn window (port).
 */

/* On Elementary OS, OS application height */
#define OS_HEADER_MARGIN 30
/* gtk's TOPLEVEL origin point assumed same on all WM */
#define GTK_WINDOW_OFFSET_X  79
#define GTK_WINDOW_OFFSET_Y  35

/* Amount limiting User's Sessions */
#define LCISESSION_LIMIT 16
#define NEW_WINDOW_WIDTH 650
#define NEW_WINDOW_HEIGHT 400


typedef struct _LciSession {
  GtkWidget       *main_window;
    // window management not part of GtkWindow
  int sslot;                      // LCISession's session number
  int order;                      // position compared to others on screen
  int maximized;                  // main_window status
  int pt_x, pt_y, sz_x, sz_y;     // main_window position/size
} LCISession;

static LCISession *session_stack[LCISESSION_LIMIT];
static int nsessions;
static char name_of_session[32] = { "Sessions" };
static int total_created_sessions = 0;


static void  lci_create_session(void);

/* Captures <control><shift><n> to create a new session window */
static gboolean
lci_keypress(GtkWidget *widget, GdkEventKey *event, LCISession *session) {

  if (((nsessions + 1) <= LCISESSION_LIMIT) && (event->keyval == GDK_KEY_N)) {
    if ((event->state & GDK_SHIFT_MASK) && (event->state & GDK_CONTROL_MASK)) {
      lci_create_session();
      return TRUE;
    }
  }
  return FALSE;
}

/* Captures gdk GDK_FOCUS_CHANGE event for keeping track of session window order */
static gboolean
lci_reorder_session(GtkWidget *widget, GdkEvent *event, LCISession *session) {

    // all above this session(moved foreground), move bakwards
  if ((event->type == GDK_FOCUS_CHANGE) && (event->focus_change.in == TRUE)) {
    int order = session->order;
    for (int idx = 0; ((idx < LCISESSION_LIMIT) && (session_stack[idx] != NULL)); idx++)
      if (session_stack[idx]->order < order)  session_stack[idx]->order++;
    session->order = 0;
  }
  return FALSE;
}

/* Captures gdk GDK_CONFIGURE and GDK_WINDOW_STATE event
 * for keeping track of session window position and size */
static gboolean
lci_update_session(GtkWidget *widget, GdkEvent *event, LCISession *session) {

  if (event->type == GDK_CONFIGURE) {
    if (session->maximized == 0) {
      session->pt_x = event->configure.x + GTK_WINDOW_OFFSET_X;
      session->pt_y = event->configure.y + GTK_WINDOW_OFFSET_Y + OS_HEADER_MARGIN;
      session->sz_x = event->configure.width - (GTK_WINDOW_OFFSET_X * 2);
      session->sz_y = event->configure.height - OS_HEADER_MARGIN - 161;
    }
  } else if (event->type == GDK_WINDOW_STATE) {
    int blocked = GDK_WINDOW_STATE_WITHDRAWN |
                  GDK_WINDOW_STATE_MAXIMIZED |
                  GDK_WINDOW_STATE_FULLSCREEN;
    session->maximized = ((event->window_state.new_window_state & blocked) != 0);
  } else {
puts("update GDK_ANY");
  }
  return FALSE;
}

/* A single save, or 'quit' save will always open file
 * in "w"rite mode. On 'quit' multiple sessions after
 * the first enter under "a"ppend.
 *  A header line is created noting session window placement.
 * Must use fact that, window closure sequence is last creation
 * to newest, not foreground to farest in background. In header
 * first output is the total session windows, then <space>
 * seperated forground to background positions. This is done
 * placing in a sequence from first created to last created.
 * This allows recreation by the second header number output
 * respresents the line number additional data found, based on
 * being the 1st of the numbers, and the actual number represents
 * the sequence recreation should occur to achieve fore to back
 * ground viewing.
 */
static void
lci_save_session(FILE *wh, LCISession *session, char *mode) {
    // want a header, current state of sessions
  if (*mode == 'w') {
      // walk sessions for sequence
      // assumption: never more than 16 sessions 6*3+10*2+1=39+nsessions
    char seqstr[42];
    int pdx = 1, idx = nsessions - 1;
      // <space> seperated numbers
    memset(seqstr, ' ', sizeof(seqstr));
    seqstr[41] = 0;
      // write out total number of sessions
    if (nsessions > 9) {
      seqstr[pdx] = '1', pdx++;
      seqstr[pdx] = (nsessions - 10) + '0', pdx++;
    } else
      seqstr[pdx] = nsessions + '0', pdx++;
    pdx++;
    do {
        // since 0 == foreground, (nsessions - 1) == bottom-most
        // position to be load order, bottom to top
      int position = session_stack[idx]->order;
      if (position > 9) {
        seqstr[pdx] = '1', pdx++;
        seqstr[pdx] = (position - 10) + '0', pdx++;
      } else
        seqstr[pdx] = position + '0', pdx++;
      pdx++;
      if ((--idx) < 0)  break;
    } while (1);
    seqstr[(pdx - 1)] = 0;
    fprintf(wh, "#%s\n", seqstr);
  }
  fprintf(wh, "%d %d %d %d \"%s\"\n",
               session->pt_x, session->pt_y,
               session->sz_x, session->sz_y,
               gtk_window_get_title(GTK_WINDOW(session->main_window)));
}

/* Support for lci_close_session(), this accesses clock
 * to determine a 'quit' signal event. Gala WM sends
 * multiple 'destroy' singles for each window.
 * To save multiple sessions, need to append open file write
 * versus a single session write.
 */
static char *
lci_save_mode(char **mode) {

  static struct timespec last_tp = {0,0};
  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);

  int append = 0;
  if ((last_tp.tv_nsec != 0) && (last_tp.tv_sec != 0)) {
    if ((tp.tv_nsec - last_tp.tv_nsec) < 0) {
      if ( ((tp.tv_sec - last_tp.tv_sec - 1) == 0) &&
           ((tp.tv_nsec - last_tp.tv_nsec + 1000000000) < 250000000) ) {
        append = 1;
      }
    } else {
      if ( ((tp.tv_sec - last_tp.tv_sec) == 0) &&
           ((tp.tv_nsec - last_tp.tv_nsec) < 250000000) ) {
        append = 1;
      }
    }
  }
  last_tp.tv_sec = tp.tv_sec;
  last_tp.tv_nsec = tp.tv_nsec;
  return *mode = (append ? "a" : "w");
}

static void
lci_close_session(GtkWidget *widget, LCISession *session) {

  char *mode;
  FILE *wh = fopen("session.lcode", lci_save_mode(&mode));
  if (wh == NULL)  puts("ERROR: unable to save session");
  else {
    lci_save_session(wh, session, mode);
    fclose(wh);
  }

  if (nsessions > 1) {
      // remove session->sslot
    int idx = session->sslot;
    int order = session->order;
    if (session->sslot != (nsessions - 1)) {
      do {
        session_stack[idx] = session_stack[(idx + 1)];
        session_stack[idx]->sslot = idx;
      } while ((++idx) < (nsessions - 1));
    }
    session_stack[idx] = NULL;
    gtk_widget_destroy(session->main_window);
    free(session);
    nsessions--;
    for (int idx = 0; ((idx < LCISESSION_LIMIT) && (session_stack[idx] != NULL)); idx++)
      if (session_stack[idx]->order > order)  session_stack[idx]->order--;
    return;
  }
  gtk_widget_destroy(session->main_window);
  free(session);
  gtk_main_quit();
}

static char *
lci_name_session(void) {

  if ((++total_created_sessions) == 1)
    return name_of_session;
  sprintf(&name_of_session[8], " - Session %d", total_created_sessions);
  return name_of_session;
}

static void
lci_connect_session(LCISession *session, char *session_name) {

  GtkWidget *main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  session->main_window = main_window;
  gtk_window_set_title(GTK_WINDOW(main_window), session_name);
    // size/position obtained from last session
  gtk_window_set_default_size(GTK_WINDOW(main_window), session->sz_x, session->sz_y);
  g_signal_connect(G_OBJECT(main_window), "destroy",
                                      G_CALLBACK(lci_close_session), session);

    // monitor user preference of size/position
  gtk_window_move(GTK_WINDOW(main_window), session->pt_x, session->pt_y);
  gtk_widget_add_events(main_window, GDK_STRUCTURE_MASK);
  g_signal_connect(G_OBJECT(main_window), "configure-event",
                                      G_CALLBACK(lci_update_session), session);
    // will catch focus, but multiple signals (does a dance, gtk)
  g_signal_connect(G_OBJECT(main_window), "window-state-event",
                                      G_CALLBACK(lci_update_session), session);

    // multiple windows (sessions)
  g_signal_connect(G_OBJECT(main_window), "key-press-event",
                                      G_CALLBACK(lci_keypress), session);
  gtk_widget_add_events(main_window, GDK_FOCUS_CHANGE_MASK);
  g_signal_connect(G_OBJECT(main_window), "focus-in-event",
                                      G_CALLBACK(lci_reorder_session), session);

  /*
   * This is point where you add routine to attach your
   * user interfaces to create your application. Boxes, menus,
   * different views, are attached to GTK_CONTAINER(session->main_window).
   *    lci_create_interface(LCISession *session);
   * Up to you the structures you add to maintain widgets, as is
   * keeping 'session_stack' static, global or allocation of its
   * memory usage. Could even add session_stack location to
   * struct _LciSession to pass to routines.
   */

    // lets see it
  gtk_widget_show_all(main_window);
}

/* Simple set up of initialization for a window location
 * on a display
 */
static void
lci_position_session(LCISession *session) {

    // main_window size/position
    // inital launch vs existing
  session->maximized = 0;
  session->sz_x = NEW_WINDOW_WIDTH, session->sz_y = NEW_WINDOW_HEIGHT;

  GdkRectangle workarea = {0};
  gdk_monitor_get_workarea(
            gdk_display_get_primary_monitor(gdk_display_get_default()),
            &workarea);
    // position based on existing sessions
  if (nsessions == 0) {
    session->pt_x = (workarea.width - session->sz_x) / 2;
    session->pt_y = (workarea.height - session->sz_y) / 2;
  } else {
      // offset window based on last window created
      // should be active?
    LCISession *base = session_stack[(nsessions - 1)];
    session->pt_x = base->pt_x + OS_HEADER_MARGIN;
    session->pt_y = base->pt_y + OS_HEADER_MARGIN;
    if (((session->sz_x + session->pt_x) > workarea.width) ||
        ((session->sz_y + session->pt_y) > workarea.height))
      session->pt_x = (session->pt_y = 0);
  }
}

/* Create a session: Tracking and interface for
 * a single viewport of application.
 */
static void
lci_create_session(void) {

  LCISession *session = malloc(sizeof(LCISession));
  session_stack[nsessions] = session;
  session_stack[nsessions]->sslot = nsessions;
  session_stack[nsessions]->order = 0;
  if (nsessions > 0) {
    int odx = nsessions;
    do session_stack[(--odx)]->order++; while (odx > 0);
  }

  lci_position_session(session);
  lci_connect_session(session, lci_name_session());
  nsessions++;
}

/* Loads last session(s) based on, and way, you
 * saved data on application 'quit'.
 */
static void
lci_restore_session(FILE *rh) {

  int  order[LCISESSION_LIMIT];
  int points[LCISESSION_LIMIT][4];
  char title[LCISESSION_LIMIT][64];
  int session_count, line_no;

  fscanf(rh, "#%d", &session_count);
  for (int idx = 0; idx < session_count; idx++)
    fscanf(rh, "%d", &order[idx]);
  fscanf(rh, "%c", &title[0][0]);
  for (int idx = 0; idx < session_count; idx++)
    fscanf(rh, "%d %d %d %d \"%[^\"]\"\n",
               &points[idx][0], &points[idx][1], &points[idx][2],
               &points[idx][3], title[idx]);

  for (int idx = 0; idx < session_count; idx++) {
      // find bottom, out first
    int odx = 0;
            // i1 2 1 == 1
    while (order[odx] != ((session_count - 1) - idx)) odx++;
    LCISession *session = malloc(sizeof(LCISession));
    session_stack[idx] = session;
    session->sslot = idx;
    session->order = order[odx];
    session->pt_x = points[odx][0];
    session->pt_y = points[odx][1];
    session->sz_x = points[odx][2];
    session->sz_y = points[odx][3];
    lci_connect_session(session, title[odx]);
    nsessions++;
    total_created_sessions++;
  }
}

int
main(int argc, char *argv[]) {

    // initialize session tracking
  nsessions = LCISESSION_LIMIT;
  do session_stack[(--nsessions)] = NULL; while (nsessions != 0);

    // initialize gtk
  gtk_init(&argc, &argv);

    // start up a new or existing session
  FILE *rh = fopen("session.lcode", "r");
  if (rh != NULL) {
    lci_restore_session(rh);
    fclose(rh);
  } else
  lci_create_session();

    // run event tracker
  gtk_main();

  return 0;
}

