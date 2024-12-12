#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <iostream>
#include <exception>


extern "C" {
    # include "../minilibx-linux/mlx.h"
}

int	closeWindow();
int handle_esc_key(int keycode);
int handle_esc_key();


class Window {
    private:
        void *mlx;
        void *win;

    public:
        Window(int width, int height, const char *title) {
            mlx = mlx_init();
            if (!mlx)
                throw std::runtime_error("Failed to initialize mlx.");
            win = mlx_new_window(mlx, width, height, const_cast<char *>(title));
            if (!win)
                throw std::runtime_error("Failed to create window.");
        }

        ~Window() {
            if (win) mlx_destroy_window(mlx, win);
        }

        void loop() {
            mlx_loop(mlx);
        }
        void mlxXHook() {
            mlx_hook(win, 17, 0L, closeWindow, NULL);
        }
        // void mlxEscKeyHook() {
        //     if (win == nullptr) {
        //         std::cerr << "Error: Window is not initialized.\n";
        //         return;
        //     }
        //     mlx_hook(win, 2, 1L << 0, handle_esc_key, NULL);
        // }
};

#endif