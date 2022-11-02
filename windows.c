// gcc `pkg-config --cflags gtk+-3.0` -o windows windows.c `pkg-config --libs gtk+-3.0`
#include <gtk/gtk.h>
#include <pwd.h>
#include <sys/stat.h>

// created event to handle missing gdk/window manager ability
#define GDK_QUIT GDK_EVENT_LAST

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
/* Save/Open Sessions
 *   Creates a 'master' file in user's home directory. This is Linux's
 *   preferences directory. The 'master' file saves session display
 *   sequence and session directory paths, where individual session
 *   data is stored. Plain editor windows use preference area, and
 *   named projects use the named project directory. Named project
 *   directories include the session save file along with user files
 *   the user wants in the directory and build objects. User does not
 *   have to use directory to house files, can reference files to include
 *   into project, or use combination of actual/referenced files.
  editor => {$HOME}/.lcode/named_session.lproj
  project => path/name_project.lproj
 */

/* Amount limiting User's Sessions */
#define LCISESSION_LIMIT 16
#define NEW_WINDOW_WIDTH 650
#define NEW_WINDOW_HEIGHT 400

typedef struct _LciSession {
  GtkWidget       *main_window;
    // interface additions to main_window
//  GtkWidget       *main_box;        // splits main_window in 2
//  LCIMenus        *menus;           // top of split
//  GtkWidget       *paned;           // bottom of split, farther split in 2
//  LCITreeArea     *tree_area;       // left of second split
//  LCIEditorArea   *editor_area;     // right of second split
    // window management not part of GtkWindow
  char            *project_name;    // if not NULL, is a project instead of editor
  char            *session_file;  // Location of work/save for session
  int sslot;                      // LCISession's session number
  int order;                      // position compared to others on screen
  int maximized;                  // main_window status
  int pt_x, pt_y, sz_x, sz_y;     // main_window position/size
    // more interface additions
//  int pd_x;
//  GtkClipboard  *clipboard;       // selection/DnD copying
} LCISession;

  // Global access to these, interface accessible
LCISession *  lci_session_create(char *);
LCISession *  lci_session_open(char *);
gboolean      lci_session_close(GtkWidget *, GdkEvent *, LCISession *);
void          lci_session_quit(LCISession *);

  // start of private
/* On Elementary OS, OS application height, was 30 */
#define OS_HEADER_MARGIN 33
/* gtk's TOPLEVEL origin point assumed same on all WM */
#define GTK_WINDOW_OFFSET_X  79
#define GTK_WINDOW_OFFSET_Y  35

static LCISession *session_stack[LCISESSION_LIMIT];
static int nsessions;
static char name_of_session[32] = { "Sessions" };
static int total_created_sessions = 0;
#define NOS_APPEND  8
static char master_file[1024] = { '.', '/', 0 };


/* inerface type examples */
static gboolean
session_interface_pane_status(GtkPaned *widget,
                              GtkScrollType scroll_type,
                              LCISession *session) {
//  session->pd_x = gtk_paned_get_position(widget);
  return FALSE;
}

  // set desired font in an interface
static void
do_try_font(void) {
}
  // where a textwindow should go, ability to swap in different ones
static GtkWidget *
session_textarea_create(LCISession *session) {
  GtkWidget *editor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  do_try_font();
  return editor_box;
}
  // where display a treeview
static GtkWidget *
session_treearea_create(LCISession *session) {
  GtkWidget *tree_window = gtk_scrolled_window_new(NULL, NULL);
  return tree_window;
}
  // create what main_window will display
static void
session_interface_create(LCISession *session) {
  GtkWidget *tree_window = session_treearea_create(session);
  GtkWidget *editor_box = session_textarea_create(session);
}


/* Captures <control><shift><n> to create a new session window */
static gboolean
session_keypress(GtkWidget *widget, GdkEventKey *event, LCISession *session) {

  if (((nsessions + 1) <= LCISESSION_LIMIT) && (event->keyval == GDK_KEY_N)) {
    if ((event->state & GDK_SHIFT_MASK) && (event->state & GDK_CONTROL_MASK)) {
      lci_session_create(NULL);
      return TRUE;
    }
  }
  return FALSE;
}

/* Captures gdk GDK_FOCUS_CHANGE event for keeping track of session window order.
 * Also use to check focus-in active port has been externally changed. */
/* Any click on window header will send a double event of TRUE.
 * Actually sends focus out, focus in, focus out, focus in */
/* guess need to attach "click" for headerbar, but noway to get headerbar widget */
static gboolean
session_reorder(GtkWidget *widget, GdkEvent *event, LCISession *session) {

    // all above this session(moved foreground), move bakwards
  if (event->type == GDK_FOCUS_CHANGE) {
      // want only 1 focus_change.in when header clicked
      // note change by click on any part other than header sends once
    int order = session->order;
    if (order != 0) {
      for (int idx = 0;
          ((idx < LCISESSION_LIMIT) && (session_stack[idx] != NULL)); idx++)
        if (session_stack[idx]->order < order)  session_stack[idx]->order++;
      session->order = 0;
    } else {
        // needed for correct behavior of session not switched
        // but instead was window (titlebar, resized) clicked
//      if (session->editor_area->editor_view[0] != NULL)
//        session->editor_area->editor_view[0]->focus_state = GTK_STATE_FLAG_NORMAL;
    }
//    lci_textport_check_status(session);
  }
    // need false else text caret won't show
  return FALSE;
}

/* Captures gdk GDK_CONFIGURE and GDK_WINDOW_STATE event
 * for keeping track of session window position and size */
static gboolean
session_update(GtkWidget *widget, GdkEvent *event, LCISession *session) {

  if (event->type == GDK_CONFIGURE) {
    if (session->maximized == 0) {
        // wayland will ignore these, gdk 0,0
        // on x11 it will honor
      session->pt_x = event->configure.x;
      session->pt_y = event->configure.y - OS_HEADER_MARGIN;
        // because of wayland, gdk wrong
      //session->sz_x = event->configure.width;
      //session->sz_y = event->configure.height;
        // works for both
      gtk_window_get_size(GTK_WINDOW(widget),
                          &session->sz_x,
                          &session->sz_y);
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
static int
session_save(FILE *wh, LCISession *session, char *mode) {
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

  fprintf(wh, "\"%s\"\n", session->session_file);
  char *session_dir = strdup(session->session_file);
  char *tptr = strrchr(session_dir, '/');
  tptr[1] = 0;
  mkdir(session_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  free(session_dir);
    // write to file in directory
  FILE *sh = fopen(session->session_file, "w");
  int response = GTK_RESPONSE_ACCEPT;
  if (sh != NULL) {
    fprintf(sh, "%d %d %d %d "
//                "%d "
                "\"%s\"\n",
                 session->pt_x, session->pt_y,
                 session->sz_x, session->sz_y,
                       // interface addition
//                 session->pd_x,
                 gtk_window_get_title(GTK_WINDOW(session->main_window)));
//    if (lci_textport_flatten(sh, session) == GTK_RESPONSE_CANCEL)
//      response = GTK_RESPONSE_CANCEL;
//    lci_treeport_flatten(sh, session);
    fclose(sh);
  }
  if (response == GTK_RESPONSE_CANCEL)
    printf("response return is cancel\n");
  return response;
}

/* Support for lci_session_close(), this accesses clock
 * to determine a 'quit' signal event. Gala WM sends
 * multiple 'destroy' singles for each window.
 * To save multiple sessions, need to append open file write
 * versus a single session write.
 */
static char *
session_save_mode(char **mode) {

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

/* Opens master file, writes window displayed sequence,
 * and allows session_save() to save session's data to file.
 * Removes window, adjusts ordering of remaining windows, and
 * frees resources of closing session.
 *   Wm closes by 'delete' in 'order of creation' (LIFO).
 * For consistancy, 'quit' does the same.
 */
gboolean
lci_session_close(GtkWidget *widget, GdkEvent *event, LCISession *session) {

  char *mode;
  FILE *wh = fopen(master_file, session_save_mode(&mode));
quit_event:
  if (wh == NULL) {
    puts("ERROR: unable to save session");
  } else {
    // will save 'session' file's current state
    // response can signal user canceled due to unsaved changes
    int response = session_save(wh, session, mode);
    fclose(wh);
    if (response == GTK_RESPONSE_CANCEL)
      return TRUE;
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
    nsessions--;
    gtk_widget_destroy(session->main_window);
    free(session->session_file);
    free(session);
    for (int idx = 0;
          ((idx < LCISESSION_LIMIT) && (session_stack[idx] != NULL)); idx++)
      if (session_stack[idx]->order > order)  session_stack[idx]->order--;

    if (event->type == GDK_QUIT) {
      mode = "a";
      wh = fopen(master_file, mode);
      session = session_stack[(nsessions - 1)];
      goto quit_event;
    }

    return FALSE;
  }
    // make nsessions 0, gtk_main_quit() does return
  nsessions--;
  gtk_widget_destroy(session->main_window);
  free(session->session_file);
  free(session);
  gtk_main_quit();
  return FALSE;
}

/* gdk, and window managers lack support for multi-window apps */
void
lci_session_quit(LCISession *top_session) {

  (void)top_session;

  GdkEvent *event = gdk_event_new(GDK_DELETE);
  event->type = GDK_QUIT;
  lci_session_close(NULL, event, session_stack[(nsessions - 1)]);
    // reaches here on a cancel of save by user in 'close'
  gdk_event_free(event);
}

/* Assign a name to new editor session */
static char *
session_name(void) {

  if ((++total_created_sessions) == 1)
    return name_of_session;
  sprintf(&name_of_session[NOS_APPEND],
               " - Session %d", total_created_sessions);
  return name_of_session;
}

/* Connects new or previous session data to a draw port */
static void
session_connect(LCISession *session, char *session_name) {

  GtkWidget *main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  session->main_window = main_window;
  gtk_window_set_title(GTK_WINDOW(main_window), session_name);
  gtk_window_set_default_size(GTK_WINDOW(main_window),
                                      session->sz_x, session->sz_y);
  gtk_window_move(GTK_WINDOW(main_window), session->pt_x, session->pt_y);
  g_signal_connect(G_OBJECT(main_window), "delete-event",
                                  G_CALLBACK(lci_session_close), session);
      // monitor user preference of size/position
  gtk_widget_add_events(main_window, GDK_STRUCTURE_MASK);
  g_signal_connect(G_OBJECT(main_window), "configure-event",
                                  G_CALLBACK(session_update), session);
    // will catch focus, but multiple signals (does a dance, gtk)
  g_signal_connect(G_OBJECT(main_window), "window-state-event",
                                  G_CALLBACK(session_update), session);
    // multiple windows (sessions)
  g_signal_connect(G_OBJECT(main_window), "key-press-event",
                                  G_CALLBACK(session_keypress), session);
  gtk_widget_add_events(main_window, GDK_FOCUS_CHANGE_MASK);
  g_signal_connect_after(G_OBJECT(main_window), "focus-in-event",
                                  G_CALLBACK(session_reorder), session);

  /*
   * This is point where you add routine to attach your
   * user interfaces to create your application. Boxes, menus,
   * different views, are attached to GTK_CONTAINER(session->main_window).
   */
  session_interface_create(session);
   /* Up to you the structures you add to maintain widgets, as is
   * keeping 'session_stack' static, global or allocation of its
   * memory usage. Could even add session_stack location to
   * struct _LciSession to pass to routines.
   */
}

/* Attempt to create session without flicker */
/* To be called when open of a pre-existing session file */
LCISession *
lci_session_open(char *named_session) {

  LCISession *session = malloc(sizeof(LCISession));
  session_stack[nsessions] = session;
  session_stack[nsessions]->sslot = nsessions;
  session_stack[nsessions]->order = 0;
  if (nsessions > 0) {
    int odx = nsessions;
    do session_stack[(--odx)]->order++; while (odx > 0);
  }
  session->project_name = NULL;
  session->session_file = strdup(named_session);
    /* extract data from file, position/name */
  char title[128];
  FILE *sh = fopen(session->session_file, "r");
  fscanf(sh, "%d %d %d %d "
//             "%d "
             "\"%[^\"]\"\n",
             &session->pt_x, &session->pt_y,
             &session->sz_x, &session->sz_y,
//             &session->pd_x,
             title);
  session_connect(session, title);
    // get rest of session data
//  lci_textport_unflatten(sh, session);
//  lci_treeport_unflatten(sh, session);
  fclose(sh);
    // sequence present,show for focus on window
  gtk_window_present(GTK_WINDOW(session->main_window));
  gtk_widget_show_all(session->main_window);
  nsessions++;
  total_created_sessions++;
  return session;
}

/* Simple set up of initialization for a window location
 * on a display
 */
static void
session_position(LCISession *session) {

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
      // interface addition
//    session->pd_x = session->sz_x / 5;
  } else {
      // offset window based on last window created
    LCISession *base = session_stack[(nsessions - 1)];
    session->pt_x = base->pt_x + OS_HEADER_MARGIN;
    session->pt_y = base->pt_y + OS_HEADER_MARGIN;
    if (((session->sz_x + session->pt_x) > workarea.width) ||
        ((session->sz_y + session->pt_y) > workarea.height))
      session->pt_x = (session->pt_y = 0);
      // interface addition
//    session->pd_x = session->sz_x / 5;
  }
}

/* Create a session: Tracking and interface for
 * a single viewport of application.
 * This has no previous data on views, if 'named_session' == NULL,
 * so default blank.
 * 'named_session' is used to determine if editor or project session.
 * This will not open, but overwrite.
 */
LCISession *
lci_session_create(char *named_session) {

  LCISession *session = malloc(sizeof(LCISession));
  session_stack[nsessions] = session;
  session_stack[nsessions]->sslot = nsessions;
  session_stack[nsessions]->order = 0;
  if (nsessions > 0) {
    int odx = nsessions;
    do session_stack[(--odx)]->order++; while (odx > 0);
  }

  if (named_session == NULL) {
      // based off user's home directory, create session's area
    session->project_name = NULL;
    char *appendptr = strrchr(master_file, '/') + 1;
    named_session = session_name();
    sprintf(appendptr, "%s/session.lproj", named_session);
    session->session_file = strdup(master_file);
    strcpy(appendptr, "session.lproj");
  } else {
      // deal with new project
    char hold[1024];
    session->project_name = strdup(strrchr(named_session, '/') + 1);
    mkdir(named_session, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    strcpy(hold, named_session);
    strcat(hold, "/session.lproj");
    session->session_file = strdup(hold);
    named_session = session->project_name;
  }
  session_position(session);
  session_connect(session, named_session);
  nsessions++;
  gtk_widget_show_all(session->main_window);
  gtk_window_present(GTK_WINDOW(session->main_window));
  return session;
}

/* Loads last session(s) based on, and way, you
 * saved data on application 'quit'.
 * Load previous user state, based on 'master' file.
 */
static int
session_restore(FILE *rh) {

  int  order[LCISESSION_LIMIT];
  char scan_line[1024];
  char *paths[LCISESSION_LIMIT];
  int session_count;

  if (fscanf(rh, "#%d", &session_count) == EOF)
    return 1;
  for (int idx = 0; idx < session_count; idx++)
    fscanf(rh, "%d", &order[idx]);
  fscanf(rh, "%c", &scan_line[0]);

  for (int idx = 0; idx < session_count; idx++) {
    if (fscanf(rh, "\"%[^\"]\"\n", scan_line) == EOF)
      break;
    paths[idx] = strdup(scan_line);
  }

  for (int idx = 0; idx < session_count; idx++) {
      // find bottom, out first
    int odx = 0;
            // i1 2 1 == 1
    while (order[odx] != ((session_count - 1) - idx)) odx++;
    LCISession *session = malloc(sizeof(LCISession));
    session_stack[idx] = session;
    session->sslot = idx;
    session->order = order[odx];
    session->project_name = NULL;

    session->session_file = paths[odx];
    char title[128];
    FILE *sh = fopen(session->session_file, "r");
    fscanf(sh, "%d %d %d %d "
//               "%d "
               "\"%[^\"]\"\n",
               &session->pt_x, &session->pt_y,
               &session->sz_x, &session->sz_y,
//               &session->pd_x,
               title);
    session_connect(session, title);
      // get rest of session data
//    lci_textport_unflatten(sh, session);
//    lci_treeport_unflatten(sh, session);
    fclose(sh);
    gtk_widget_show_all(session->main_window);
    nsessions++;
    total_created_sessions++;
  }
  return 0;
}

static void
session_master(char *master) {

//  char *homedir;
//  uid_t uid = getuid();
//  struct passwd *pw = getpwuid(uid);
//  homedir = (pw != NULL) ? pw->pw_dir : getenv("HOME");
//  if (homedir != NULL) {
//    strcpy(master, homedir);
//    strcat(master, "/.lcode/");
//    mkdir(master, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
//  }
  strcat(master, "session.lproj");
}

int
main(int argc, char *argv[]) {

    // initialize session tracking
  nsessions = LCISESSION_LIMIT;
  do session_stack[(--nsessions)] = NULL; while (nsessions != 0);

    // initialize gtk
  gtk_init(&argc, &argv);

    // there is only one session 'master'
    // it belongs to the 'user'
  session_master(master_file);

    // start up a new or existing session
  FILE *rh = fopen(master_file, "r");
  if (rh != NULL) {
      // possible error of blank data
    if (session_restore(rh))
      lci_session_create(NULL);
    fclose(rh);
  } else {
    lci_session_create(NULL);
  }

    // run event tracker
  gtk_main();

  return 0;
}

