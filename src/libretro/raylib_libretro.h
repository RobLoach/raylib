#if defined(PLATFORM_LIBRETRO)
#ifndef RAYLIB_LIBRETRO_H_
#define RAYLIB_LIBRETRO_H_

static void libretroCloseWindow();
static void libretroSetWindowSize(int width, int height);
static void libretroSetTargetFPS(int fps);
static void libretroEndDrawing();
static void libretroPollInputEvents();

#endif
#endif