# Sessions
gtk+ multi-window application creation/restore

Basic window(s) creation, window management

create multiple windows: <control><shift><n>

quit: window manager 'Close All' for multiple, 'Close' for single

Will create a save file allowing position/size rememberance

  create:
gcc \`pkg-config --cflags gtk+-3.0\` -o windows windows.c \`pkg-config --libs gtk+-3.0\`

  run:
./windows

Note: could not find way to access close widget on gtk header bar to
create a 'Close All' quit from header. Assume one must create one
and hide decorations?
