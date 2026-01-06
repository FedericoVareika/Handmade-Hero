#include <stdint.h>
#include <stdio.h>

#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/keysym.h>

#include "handmade.h"
#include "handmade.c"

// TODO(fede): Remove global variables
global bool running = true;
global Atom wm_delete_window;
global u32 x_offset = 0;
global u32 y_offset = 0;

// TODO(fede): Add pitch maybe?
typedef struct {
    u32 width;
    u32 height;
    u32 bits_per_pixel;
    u32 capacity;
    u32 *data;
} XBackbuffer;

internal void x11_resize_backbuffer(XBackbuffer *buffer, Display *display,
                                    XImage **image, i32 width, i32 height) {
    if (*image) {
        (*image)->data = 0;
        XDestroyImage(*image);
    }

    u32 new_size = width * height * buffer->bits_per_pixel / 8;
    if (buffer->capacity < new_size) {
        munmap(buffer->data, buffer->capacity);
        buffer->data = 0;
    }

    buffer->width = width;
    buffer->height = height;

    if (!buffer->data) {
        buffer->data = mmap(0, new_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        buffer->capacity = new_size;
    }

    u32 default_screen = XDefaultScreen(display);
    *image = XCreateImage(display, XDefaultVisual(display, default_screen),
                          XDefaultDepth(display, default_screen), ZPixmap, 0,
                          (char *)buffer->data, buffer->width, buffer->height,
                          32, 0);
}

internal bool x11_has_shm_ext(Display *display) {
    return XShmQueryExtension(display) == True;
}

internal XImage *x11_create_shm_image(Display *display, XBackbuffer buffer,
                                      XShmSegmentInfo *shm_segment_info) {

    u32 default_screen = XDefaultScreen(display);
    XImage *image = XShmCreateImage(
        display, DefaultVisual(display, default_screen),
        XDefaultDepth(display, default_screen), ZPixmap, (char *)buffer.data,
        shm_segment_info, buffer.width, buffer.height);

    shm_segment_info->shmid = shmget(
        IPC_PRIVATE, image->bytes_per_line * image->height, IPC_CREAT | 0777);
    shm_segment_info->shmaddr = image->data =
        shmat(shm_segment_info->shmid, 0, 0);
    shm_segment_info->readOnly = false;

    if (!XShmAttach(display, shm_segment_info)) {
        fprintf(stderr, "Could not attach to shared memory\n");
    }

    return image;
}

internal void x11_handle_events(Display *display, XImage **image,
                                XBackbuffer *buffer) {
    while (XPending(display)) {
        XEvent event;
        XNextEvent(display, &event);

        switch (event.type) {
        case ClientMessage: {
            XClientMessageEvent message = event.xclient;
            if ((Atom)event.xclient.data.l[0] == wm_delete_window) {
                running = false;
                printf("Delete window\n");
            } else {
                printf("Unrecognized client message\n");
            }
        } break;
        case ConfigureNotify: {
            XConfigureEvent configure = event.xconfigure;
            x11_resize_backbuffer(buffer, display, image, configure.width,
                                  configure.height);
        } break;
        case KeyPress: {
            XKeyPressedEvent press = event.xkey;
            KeySym key = XLookupKeysym(&press, 0);

            switch (key) {
            case XK_w:
            case XK_W:
            case XK_Up: {
            } break;
            }

        } break;
        default: {
            printf("Unhandled event: %d\n", event.type);
        } break;
        }
    }
}

int main(void) {
    Display *display = XOpenDisplay(0);

    /* TODO(fede): Check if I need to change to shm for performance reasons
    if (!x11_has_shm_ext(display)) {
        fprintf(stderr,
                "ERROR: Current display does not support MIT-SHM extension.\n");
        return 0;
    }
    */

    XBackbuffer buffer = {
        .bits_per_pixel = 32,
    };

    XImage *image = 0;
    x11_resize_backbuffer(&buffer, display, &image, 1280, 720);

    int default_screen = XDefaultScreen(display);
    Window window = XCreateSimpleWindow(
        display, XRootWindow(display, default_screen), 0, 0, buffer.width,
        buffer.height, 1, BlackPixel(display, default_screen),
        WhitePixel(display, default_screen));

    {
        wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display, window, &wm_delete_window, 1);

        XSelectInput(display, window,
                     KeyPressMask | KeyReleaseMask | ButtonPressMask |
                         ButtonReleaseMask | StructureNotifyMask);
    }

    XGCValues gc_values = {};
    gc_values.function = GXcopy;
    gc_values.plane_mask = AllPlanes;
    GC gc = XCreateGC(display, window, GCFunction | GCPlaneMask, &gc_values);

    XMapWindow(display, window);

    XStoreName(display, window, "Handmade Hero");

    while (running) {
        x11_handle_events(display, &image, &buffer);

        GameDisplayBuffer game_buffer = {};
        game_buffer.data = buffer.data;
        game_buffer.height = buffer.height;
        game_buffer.width = buffer.width;
        game_update_and_render(&game_buffer);

        y_offset++;
        x_offset++;

        XPutImage(display, window, gc, image, 0, 0, 0, 0, buffer.width,
                  buffer.height);
    }

    return 0;
}

/*
 * NOTE(fede): now that i "proved" that i can write to screen using X11, 
 * for the rest of the codebase i probably will use SDL2 to process input, 
 * sound, and probably rendering as well. 
 * This removes a lot of tedious work that i dont think will make me a better 
 * programmer just yet. 
 * It could be a fun process to slowly eliminate SDL though.
 */
