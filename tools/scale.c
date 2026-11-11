// Scale.sft: sharp window sizes and full screen for Knytt.
//
// Knytt's runtime loads every *.sft next to it as a sound filter. This one exports no
// filter; it stays loaded and takes over the game window's layout instead. The picture
// lives in a child window that the runtime stretches without filtering (the Stretch
// flag, set by patch-game.py), so placing that child decides the scale:
//   - Size 1x/2x/3x in the Game menu set the window to an exact multiple, saved in
//     Settings.ini.
//   - Any other window size shows the largest picture that keeps the 600:240 shape,
//     drawn here with sharp bilinear in place of the runtime's stretch.
//   - Full screen is a borderless window at the largest whole multiple, so the display
//     mode never changes.
//
// The picture is drawn by the graphics chip where it can be: it goes there at its own
// 600 x 240 and the chip scales it. Where that fails, the processor scales it instead.
//
// Build with tools/build-scale.sh.
#include <windows.h>

#define GAME_W 600
#define GAME_H 240
#define ID_FULLSCREEN 0x7da   // the game's own "Full screen" menu item
#define ID_SIZE 0x9c40        // ours: ID_SIZE + 0..2 are 1x..3x

static HWND mainWnd, pictureWnd;
static WNDPROC mainProc, pictureProc;
static UINT initMsg;
static int scale;             // 1..3, the saved window size
static BOOL fullscreen;
static HMENU savedMenu;
static LONG savedStyle;
static RECT savedRect;
static char ini[MAX_PATH];

// Where the picture goes inside the main window's client area.
static void picture_rect(RECT *out) {
  RECT c;
  GetClientRect(mainWnd, &c);
  int w = c.right, h = c.right * GAME_H / GAME_W;
  if (h > c.bottom) { h = c.bottom; w = c.bottom * GAME_W / GAME_H; }
  int n = w / GAME_W;
  // Whole multiples only in full screen, and when the window is a hair off one.
  if (n >= 1 && (fullscreen || w - n * GAME_W < 8)) { w = n * GAME_W; h = n * GAME_H; }
  out->left = (c.right - w) / 2;
  out->top = (c.bottom - h) / 2;
  out->right = w;    // width and height, not corners
  out->bottom = h;
}

static void place_picture(void) {
  RECT r;
  picture_rect(&r);
  SetWindowPos(pictureWnd, NULL, r.left, r.top, r.right, r.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
  // Redraw everything: the bars around the picture and the whole picture at its new size.
  RedrawWindow(mainWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// Size the window so its client area is exactly scale times the game, centred on the desktop.
static void apply_scale(void) {
  RECT r = {0, 0, GAME_W * scale, GAME_H * scale}, area;
  AdjustWindowRectEx(&r, GetWindowLongA(mainWnd, GWL_STYLE), GetMenu(mainWnd) != NULL, GetWindowLongA(mainWnd, GWL_EXSTYLE));
  int w = r.right - r.left, h = r.bottom - r.top;
  SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
  int x = area.left + (area.right - area.left - w) / 2, y = area.top + (area.bottom - area.top - h) / 2;
  SetWindowPos(mainWnd, NULL, x < area.left ? area.left : x, y < area.top ? area.top : y, w, h, SWP_NOZORDER);
  place_picture();
}

static void set_fullscreen(BOOL on) {
  if (on == fullscreen) return;
  fullscreen = on;
  if (on) {
    savedStyle = GetWindowLongA(mainWnd, GWL_STYLE);
    savedMenu = GetMenu(mainWnd);
    GetWindowRect(mainWnd, &savedRect);
    SetWindowLongA(mainWnd, GWL_STYLE, savedStyle & ~(WS_CAPTION | WS_THICKFRAME));
    SetMenu(mainWnd, NULL);
    SetWindowPos(mainWnd, HWND_TOP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_FRAMECHANGED);
  } else {
    SetWindowLongA(mainWnd, GWL_STYLE, savedStyle);
    SetMenu(mainWnd, savedMenu);
    SetWindowPos(mainWnd, NULL, savedRect.left, savedRect.top, savedRect.right - savedRect.left,
                 savedRect.bottom - savedRect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
  }
  place_picture();
}

// The runtime can rebuild its menu, so the size items are added whenever the menu opens.
static void update_menu(void) {
  HMENU game = GetSubMenu(GetMenu(mainWnd), 0);
  if (!game) return;
  if (GetMenuState(game, ID_SIZE, MF_BYCOMMAND) == (UINT)-1) {
    int at = 0, count = GetMenuItemCount(game);
    while (at < count && GetMenuItemID(game, at) != ID_FULLSCREEN) at++;
    for (int i = 0; i < 3; i++) {
      char text[] = "Size &1x";
      text[6] += i;
      InsertMenuA(game, at + 1 + i, MF_BYPOSITION | MF_STRING, ID_SIZE + i, text);
    }
  }
  CheckMenuRadioItem(game, ID_SIZE, ID_SIZE + 2, ID_SIZE + scale - 1, MF_BYCOMMAND);
}

static LRESULT CALLBACK main_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == initMsg) { apply_scale(); return 0; }
  switch (msg) {
    case WM_INITMENUPOPUP:
      update_menu();
      break;
    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (id == ID_FULLSCREEN) { set_fullscreen(!fullscreen); return 0; }
      if (id >= ID_SIZE && id < ID_SIZE + 3) {
        char value[2] = {'1' + id - ID_SIZE, 0};
        scale = id - ID_SIZE + 1;
        WritePrivateProfileStringA("Display", "Scale", value, ini);
        apply_scale();
        return 0;
      }
      break;
    }
    case WM_SYSKEYDOWN:   // Alt+Enter
    case WM_SYSCHAR:
      if (wp == VK_RETURN) { if (msg == WM_SYSKEYDOWN && !(lp & 0x40000000)) set_fullscreen(!fullscreen); return 0; }
      break;
    case WM_ERASEBKGND: {
      RECT c;
      GetClientRect(w, &c);
      FillRect((HDC)wp, &c, (HBRUSH)GetStockObject(BLACK_BRUSH));
      return 1;
    }
    case WM_SIZE: {
      LRESULT r = CallWindowProcA(mainProc, w, msg, wp, lp);
      place_picture();
      return r;
    }
  }
  return CallWindowProcA(mainProc, w, msg, wp, lp);
}

static LRESULT CALLBACK picture_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  if ((msg == WM_SYSKEYDOWN || msg == WM_SYSCHAR) && wp == VK_RETURN) return main_proc(mainWnd, msg, wp, lp);
  if (msg == WM_WINDOWPOSCHANGING) {   // the runtime sizes the picture to the window; overrule it
    WINDOWPOS *p = (WINDOWPOS *)lp;
    RECT r;
    picture_rect(&r);
    p->x = r.left; p->y = r.top; p->cx = r.right; p->cy = r.bottom;
    p->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
    return 0;
  }
  return CallWindowProcA(pictureProc, w, msg, wp, lp);
}

// ---- Sharp bilinear, for sizes between whole multiples ----------------------------------
// Every game pixel covers the same span of screen pixels, and only a screen pixel that a
// game pixel boundary passes through mixes the two it straddles, by how much of each it
// covers. Lines keep one even width and flat areas stay crisp, where nearest neighbour
// alternates narrow and wide columns and a general filter smears everything.

typedef struct { int j; DWORD a; } Tap;   // the first game pixel under a screen pixel, and its share /256

static Tap *tapX, *tapY;
static DWORD *rowBuf[2], *outBuf;   // two game rows scaled across, and the finished picture
static int rowIdx[2], outW, outH;
static const BYTE *srcTop;          // the game's bitmap: top row, bytes between rows, width
static int srcStride, srcW;

// (a*p + (256-a)*q) / 256 for each byte of two pixels.
static DWORD mix(DWORD p, DWORD q, DWORD a) {
  if (a >= 256) return p;
  DWORD b = 256 - a;
  DWORD rb = ((((p & 0x00ff00ff) * a + (q & 0x00ff00ff) * b) + 0x00800080) >> 8) & 0x00ff00ff;
  DWORD ag = ((((p >> 8) & 0x00ff00ff) * a + ((q >> 8) & 0x00ff00ff) * b) + 0x00800080) & 0xff00ff00;
  return rb | ag;
}

// Taps for n screen pixels across `limit` game pixels.
static void taps(Tap *t, int n, int limit) {
  for (int i = 0; i < n; i++) {
    int j = i * limit / n;
    int rest = (j + 1) * n - i * limit;   // what is left of pixel j from here on, in 1/limit screen pixels
    t[i].j = j;
    t[i].a = j >= limit - 1 || rest >= limit ? 256 : (rest * 256 + limit / 2) / limit;
  }
}

// Game row sy at screen width. Each row is scaled across once and kept while the screen
// rows that use it pass; only rows on a pixel boundary then mix two of them.
static const DWORD *across(int sy) {
  if (sy == rowIdx[0]) return rowBuf[0];
  if (sy == rowIdx[1]) return rowBuf[1];
  int use = rowIdx[0] < rowIdx[1] ? 0 : 1;   // rows only move down: replace the older one
  rowIdx[use] = sy;
  const DWORD *s = (const DWORD *)(srcTop + sy * srcStride);
  DWORD *d = rowBuf[use];
  for (int x = 0; x < outW; x++) {
    int j = tapX[x].j;
    d[x] = mix(s[j], s[j + 1 < srcW ? j + 1 : j], tapX[x].a);
  }
  return d;
}

// Draws the bitmap selected into `src` over the w x h window behind `dc`. FALSE if the
// bitmap is not the 32-bit one the runtime keeps the game's picture in.
static BOOL sharp_blit(HDC dc, int w, int h, HDC src, int sw, int sh) {
  DIBSECTION ds;
  if (GetObjectA(GetCurrentObject(src, OBJ_BITMAP), sizeof ds, &ds) != sizeof ds) return FALSE;
  int W = ds.dsBm.bmWidth, H = ds.dsBm.bmHeight;
  if (ds.dsBm.bmBitsPixel != 32 || !ds.dsBm.bmBits || W != sw || H != sh) return FALSE;
  if (w != outW || h != outH) {
    HANDLE heap = GetProcessHeap();
    if (tapX) HeapFree(heap, 0, tapX);
    tapX = HeapAlloc(heap, 0, (w + h) * sizeof(Tap) + (2 * w + w * h) * sizeof(DWORD));
    if (!tapX) { outW = 0; return FALSE; }
    tapY = tapX + w;
    rowBuf[0] = (DWORD *)(tapY + h);
    rowBuf[1] = rowBuf[0] + w;
    outBuf = rowBuf[1] + w;
    outW = w;
    outH = h;
    taps(tapX, w, W);
    taps(tapY, h, H);
  }
  srcW = W;
  srcStride = ds.dsBm.bmWidthBytes;
  srcTop = ds.dsBm.bmBits;
  if (ds.dsBmih.biHeight > 0) { srcTop += (H - 1) * srcStride; srcStride = -srcStride; }   // bottom-up
  rowIdx[0] = rowIdx[1] = -1;
  for (int y = 0; y < h; y++) {
    DWORD *o = outBuf + y * w;
    int j = tapY[y].j;
    const DWORD *r0 = across(j);
    if (tapY[y].a >= 256) { for (int x = 0; x < w; x++) o[x] = r0[x]; continue; }
    const DWORD *r1 = across(j + 1 < H ? j + 1 : j);
    for (int x = 0; x < w; x++) o[x] = mix(r0[x], r1[x], tapY[y].a);
  }
  BITMAPINFO bi;
  bi.bmiHeader = (BITMAPINFOHEADER){sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
  SetDIBitsToDevice(dc, 0, 0, w, h, 0, 0, 0, h, outBuf, &bi, DIB_RGB_COLORS);
  return TRUE;
}

// ---- Drawing on the graphics chip -------------------------------------------------------
// Scaling on the processor touches every screen pixel twice, once to stretch the picture
// and once to hand it to the display, and at 3x a Pi 400 cannot do that 50 times a second.
// Here the picture goes to the chip as a 600 x 240 texture and the chip draws it at the
// size of the picture window.
//
// At a whole multiple it repeats each pixel. At sizes in between it does that first, into
// a texture a whole number of times larger, and then draws that texture at the window's
// size with the chip's own smoothing. Only the screen pixels that straddle two game pixels
// come out blended, which is the sharp bilinear picture the code above makes. Nothing is
// worked out per pixel, since a Pi's chip is slow at arithmetic and quick at reading textures.
//
// The drawing surface belongs to the picture window. Wine shows a window's OpenGL output
// only where no child window covers it, and the picture window covers the main one.

#define GL_TEXTURE_2D 0x0de1
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812f
#define GL_RGB 0x1907
#define GL_BGRA 0x80e1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_QUADS 7
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_FRAMEBUFFER 0x8d40
#define GL_COLOR_ATTACHMENT0 0x8ce0
#define GL_FRAMEBUFFER_COMPLETE 0x8cd5
#define GL_MAX_TEXTURE_SIZE 0x0d33

// The OpenGL calls used, looked up by name so the game still starts without OpenGL.
// The first GL_CORE come from opengl32.dll, the rest from the driver once a context is current.
static struct {
  HGLRC (WINAPI *CreateContext)(HDC);
  BOOL (WINAPI *MakeCurrent)(HDC, HGLRC);
  BOOL (WINAPI *DeleteContext)(HGLRC);
  PROC (WINAPI *GetProcAddress)(LPCSTR);
  void (WINAPI *Viewport)(int, int, int, int);
  void (WINAPI *Enable)(unsigned);
  void (WINAPI *Clear)(unsigned);
  void (WINAPI *GetIntegerv)(unsigned, int *);
  void (WINAPI *GenTextures)(int, unsigned *);
  void (WINAPI *BindTexture)(unsigned, unsigned);
  void (WINAPI *TexParameteri)(unsigned, unsigned, int);
  void (WINAPI *TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
  void (WINAPI *TexSubImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
  void (WINAPI *Begin)(unsigned);
  void (WINAPI *End)(void);
  void (WINAPI *TexCoord2f)(float, float);
  void (WINAPI *Vertex2f)(float, float);
  void (WINAPI *GenFramebuffers)(int, unsigned *);
  void (WINAPI *BindFramebuffer)(unsigned, unsigned);
  void (WINAPI *FramebufferTexture2D)(unsigned, unsigned, unsigned, unsigned, int);
  unsigned (WINAPI *CheckFramebufferStatus)(unsigned);
} gl;
#define GL_CORE 17
static const char *const glNames[] = {
  "wglCreateContext", "wglMakeCurrent", "wglDeleteContext", "wglGetProcAddress", "glViewport", "glEnable",
  "glClear", "glGetIntegerv", "glGenTextures", "glBindTexture", "glTexParameteri", "glTexImage2D", "glTexSubImage2D",
  "glBegin", "glEnd", "glTexCoord2f", "glVertex2f",
  "glGenFramebuffers", "glBindFramebuffer", "glFramebufferTexture2D", "glCheckFramebufferStatus"};
#define GL_CALLS ((int)(sizeof glNames / sizeof *glNames))

static HDC glDC;                   // the picture window's, kept for good
static unsigned glPicture, glBig;  // the game's picture, and the same enlarged a whole number of times
static unsigned glBuffer;          // draws into glBig
static int glW, glH, glTimes;      // the picture's size, and how many times larger glBig is
static int glLimit;                // the widest texture the chip takes

// A texture that repeats at its edges and is read with `filter`, left bound.
static unsigned gl_texture(int filter) {
  unsigned texture;
  gl.GenTextures(1, &texture);
  gl.BindTexture(GL_TEXTURE_2D, texture);
  gl.TexParameteri(GL_TEXTURE_2D, 0x2801, filter);            // when shrunk
  gl.TexParameteri(GL_TEXTURE_2D, 0x2800, filter);            // when enlarged
  gl.TexParameteri(GL_TEXTURE_2D, 0x2802, GL_CLAMP_TO_EDGE);  // across
  gl.TexParameteri(GL_TEXTURE_2D, 0x2803, GL_CLAMP_TO_EDGE);  // and down
  return texture;
}

// The texture that is bound, drawn over everything being drawn into. `top` is the
// texture row that comes out at the top. Clearing first tells the chip that what was
// there before is not wanted, which saves it reading that back in.
static void gl_draw(float top) {
  gl.Clear(GL_COLOR_BUFFER_BIT);
  gl.Begin(GL_QUADS);
  gl.TexCoord2f(0, 1 - top); gl.Vertex2f(-1, -1);
  gl.TexCoord2f(1, 1 - top); gl.Vertex2f(1, -1);
  gl.TexCoord2f(1, top);     gl.Vertex2f(1, 1);
  gl.TexCoord2f(0, top);     gl.Vertex2f(-1, 1);
  gl.End();
}

// A context for the window behind `dc`, current on this thread and ready to draw. NULL
// if any step fails.
static HGLRC gl_context(HDC dc) {
  PIXELFORMATDESCRIPTOR pfd = {sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 24};
  int format = ChoosePixelFormat(dc, &pfd);
  if (!format || !SetPixelFormat(dc, format, &pfd)) return NULL;
  HGLRC context = gl.CreateContext(dc);
  if (!context) return NULL;
  if (gl.MakeCurrent(dc, context)) {
    void **slot = (void **)&gl;
    int i = GL_CORE;
    while (i < GL_CALLS && (slot[i] = (void *)gl.GetProcAddress(glNames[i]))) i++;
    if (i == GL_CALLS) {
      gl.Enable(GL_TEXTURE_2D);
      gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &glLimit);
      gl.GenFramebuffers(1, &glBuffer);
      glBig = gl_texture(GL_LINEAR);
      glPicture = gl_texture(GL_NEAREST);
      glW = glH = glTimes = 0;
      return context;
    }
    gl.MakeCurrent(NULL, NULL);
  }
  gl.DeleteContext(context);
  return NULL;
}

// Sets the chip up to draw in the picture window. Once a window has an OpenGL surface, what
// GDI draws in it no longer shows, so everything is tried on a throwaway window first.
static BOOL gl_start(void) {
  HMODULE lib = LoadLibraryA("opengl32.dll");
  void **slot = (void **)&gl;
  for (int i = 0; i < GL_CORE; i++)
    if (!lib || !(slot[i] = (void *)GetProcAddress(lib, glNames[i]))) return FALSE;
  HWND probe = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 8, 8, NULL, NULL, NULL, NULL);
  if (!probe) return FALSE;
  HDC dc = GetDC(probe);
  HGLRC context = gl_context(dc);
  if (context) {
    gl.MakeCurrent(NULL, NULL);
    gl.DeleteContext(context);
  }
  ReleaseDC(probe, dc);
  DestroyWindow(probe);
  if (!context) return FALSE;
  glDC = GetDC(pictureWnd);
  if (!gl_context(glDC)) return FALSE;
  // The game keeps its own time, so a frame is shown as soon as it is drawn.
  BOOL (WINAPI *interval)(int) = (BOOL (WINAPI *)(int))gl.GetProcAddress("wglSwapIntervalEXT");
  if (interval) interval(0);
  return TRUE;
}

// Draws the bitmap selected into `src`, the game's whole picture, in the picture window's
// place. FALSE if the chip cannot be used, or the bitmap is not the 32-bit one expected.
static BOOL gpu_blit(HDC src) {
  static int state;   // 0 not tried yet, 1 working, -1 failed: the processor draws from then on
  DIBSECTION ds;
  if (state < 0 || GetObjectA(GetCurrentObject(src, OBJ_BITMAP), sizeof ds, &ds) != sizeof ds) return FALSE;
  int W = ds.dsBm.bmWidth, H = ds.dsBm.bmHeight;
  if (ds.dsBm.bmBitsPixel != 32 || !ds.dsBm.bmBits || ds.dsBm.bmWidthBytes != W * 4) return FALSE;
  if (!state) state = gl_start() ? 1 : -1;
  if (state < 0) return FALSE;

  RECT r;
  picture_rect(&r);
  int w = r.right, h = r.bottom;
  if (w < 1 || h < 1) return TRUE;   // nothing to see, as when minimized
  if (W != glW || H != glH) {
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glW = W;
    glH = H;
    glTimes = 0;
  }
  gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, ds.dsBm.bmBits);
  // OpenGL counts rows from the bottom, as a bottom-up bitmap does.
  float top = ds.dsBmih.biHeight > 0 ? 1 : 0;

  // How many whole times larger to make the picture first: as many as fit in the window,
  // and in a texture. Once is no enlarging, which is right for a window smaller than 2x.
  int times = w / W < h / H ? w / W : h / H;
  while (times > 1 && (W * times > glLimit || H * times > glLimit)) times--;
  if (w % W == 0 && h % H == 0) {
    gl.Viewport(0, 0, w, h);
    gl_draw(top);
  } else if (times < 2) {
    gl.TexParameteri(GL_TEXTURE_2D, 0x2801, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, 0x2800, GL_LINEAR);
    gl.Viewport(0, 0, w, h);
    gl_draw(top);
    gl.TexParameteri(GL_TEXTURE_2D, 0x2801, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, 0x2800, GL_NEAREST);
  } else {
    gl.BindTexture(GL_TEXTURE_2D, glBig);
    if (times != glTimes) {
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W * times, H * times, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
      glTimes = times;
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, glBuffer);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glBig, 0);
    BOOL usable = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl.BindTexture(GL_TEXTURE_2D, glPicture);
    if (usable) {
      gl.Viewport(0, 0, W * times, H * times);
      gl_draw(top);
      top = 1;   // glBig has its rows the way OpenGL counts them
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, w, h);
    if (usable) gl.BindTexture(GL_TEXTURE_2D, glBig);
    gl_draw(top);
    gl.BindTexture(GL_TEXTURE_2D, glPicture);
  }
  SwapBuffers(glDC);
  return TRUE;
}

// The runtime's blit: the whole picture, stretched to the picture window. The chip draws
// it if it can. Otherwise a whole multiple goes through as it is, since the runtime's
// stretch repeats pixels exactly, and other sizes get sharp bilinear on the processor.
static BOOL WINAPI stretch_blt(HDC dc, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop) {
  if (rop == SRCCOPY && WindowFromDC(dc) == pictureWnd) {
    if (gpu_blit(src)) return TRUE;
    if (!x && !y && !sx && !sy && sw > 0 && sh > 0 && (w % sw || h % sh) && sharp_blit(dc, w, h, src, sw, sh))
      return TRUE;
  }
  return StretchBlt(dc, x, y, w, h, src, sx, sy, sw, sh, rop);
}

// The runtime copies part of the picture across when only that part has changed. The
// chip draws the whole picture each time, which covers it.
static BOOL WINAPI bit_blt(HDC dc, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop) {
  if (rop == SRCCOPY && WindowFromDC(dc) == pictureWnd && gpu_blit(src)) return TRUE;
  return BitBlt(dc, x, y, w, h, src, sx, sy, rop);
}

// Points a module's import of a GDI function at ours.
static void hook(const char *module, const char *name, void *ours) {
  BYTE *base = (BYTE *)GetModuleHandleA(module);
  void *theirs = GetProcAddress(GetModuleHandleA("gdi32.dll"), name);
  if (!base || !theirs) return;
  IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
  IMAGE_IMPORT_DESCRIPTOR *imp =
      (IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
  for (; imp->Name; imp++)
    for (void **slot = (void **)(base + imp->FirstThunk); *slot; slot++)
      if (*slot == theirs) {
        DWORD old;
        VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
        *slot = ours;
        VirtualProtect(slot, sizeof *slot, old, &old);
      }
}

// Waits for the game's windows, then takes over their window procedures and the blit.
static DWORD WINAPI attach(void *unused) {
  for (int tries = 0; tries < 600 && !pictureWnd; tries++) {
    Sleep(50);
    for (HWND w = NULL; (w = FindWindowExA(NULL, w, "Mf2MainClassTh", NULL));) {
      DWORD pid;
      GetWindowThreadProcessId(w, &pid);
      if (pid == GetCurrentProcessId()) { mainWnd = w; break; }
    }
    if (mainWnd) pictureWnd = FindWindowExA(mainWnd, NULL, "Mf2EditClassTh", NULL);
  }
  if (!pictureWnd) return 0;
  hook("mmfs2.dll", "StretchBlt", stretch_blt);
  hook("mmfs2.dll", "BitBlt", bit_blt);
  pictureProc = (WNDPROC)SetWindowLongA(pictureWnd, GWL_WNDPROC, (LONG)picture_proc);
  mainProc = (WNDPROC)SetWindowLongA(mainWnd, GWL_WNDPROC, (LONG)main_proc);
  PostMessageA(mainWnd, initMsg, 0, 0);
  return 0;
}

BOOL WINAPI _DllMainCRTStartup(HINSTANCE dll, DWORD reason, LPVOID reserved) {
  if (reason != DLL_PROCESS_ATTACH || initMsg) return TRUE;
  char self[MAX_PATH];
  GetModuleFileNameA(dll, self, MAX_PATH);
  LoadLibraryA(self);   // the runtime unloads filters it cannot use; hold on
  // Settings.ini sits next to Knytt.exe, which the command line names: /SF "<path>" /SO<offset>
  const char *from = NULL, *to = NULL;
  for (const char *p = GetCommandLineA(); *p; p++) {
    if (!from && p[0] == '/' && p[1] == 'S' && p[2] == 'F' && p[3] == ' ') from = p + 4;
    if (p[0] == ' ' && p[1] == '/' && p[2] == 'S' && p[3] == 'O') to = p;
  }
  if (from && *from == '"') from++;
  if (!from || to <= from || to - from > MAX_PATH - 16) return TRUE;
  lstrcpynA(ini, from, to - from + 1);
  char *name = ini + lstrlenA(ini);
  while (name > ini && name[-1] != '\\') name--;
  lstrcpyA(name, "Settings.ini");
  scale = GetPrivateProfileIntA("Display", "Scale", 2, ini);
  if (scale < 1 || scale > 3) scale = 2;
  initMsg = RegisterWindowMessageA("KnyttScaleInit");
  CloseHandle(CreateThread(NULL, 0, attach, NULL, 0, NULL));
  return TRUE;
}
