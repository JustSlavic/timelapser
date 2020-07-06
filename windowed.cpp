#include <X11/Xlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>


int main() {
    Display *display;
    Window window;
    XEvent e;
    const char *msg = "Hello World!";
    int screen;

    display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }

    screen = DefaultScreen(display);
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 10, 10, 800, 600, 1,
                                 BlackPixel(display, screen), WhitePixel(display, screen));
    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapWindow(display, window);

    while (true) {
        XNextEvent(display, &e);

        if (e.type == Expose) {
            XFillRectangle(display, window, DefaultGC(display, screen), 0, 0, 800 / 2, 600 / 2);
            XDrawString(display, window, DefaultGC(display, screen), 10, 50, msg, strlen(msg));
        }
        if (e.type == KeyPress)
            break;
    }

    XCloseDisplay(display);
    return 0;
}
