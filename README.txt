  QUECTO
A SOFTWARE BY THE PANDIX PROJECT

Quecto is a really simple text editor written in C. You can move
the cursor around the file, write and edit text, and save your work.
This software makes part of The Pandix Project and follows its
Manifesto and Code of Conduct when contributing.

  COMPILING
Compiling Quecto is a really simple task, you can use your
preferred C compiler like GCC, Clang, etc.
For example, with GCC:
    gcc -std=c11 -Wall -Wextra -pedantic main.c -o quecto

  RUNNING
For running Quecto you just need to put the name of the executable
and the name of the text file. Something like this:
    ./quecto file.txt

  EDITING
Use the arrow keys to move the cursor. Type to insert text, use
Backspace to delete the character before the cursor, and use
Control-D to delete the character under the cursor. Press Escape
to enter command mode, then type a command and press Enter:
    :w      Save the file
    :q      Quit if there are no unsaved changes
    :q!     Quit without saving
    :wq     Save the file and quit
Press Escape again to return to editing.

  LICENSE
Quecto is avaidable as the absolute public domain or by the
Creative Commons Zero v1.0.