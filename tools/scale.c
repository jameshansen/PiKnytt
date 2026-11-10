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

// The runtime's blit: the whole picture, stretched to the picture window. A whole multiple
// goes through as it is, since the runtime's stretch repeats pixels exactly.
static BOOL WINAPI stretch_blt(HDC dc, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop) {
  if (rop == SRCCOPY && !x && !y && !sx && !sy && sw > 0 && sh > 0 && (w % sw || h % sh) &&
      WindowFromDC(dc) == pictureWnd && sharp_blit(dc, w, h, src, sw, sh))
    return TRUE;
  return StretchBlt(dc, x, y, w, h, src, sx, sy, sw, sh, rop);
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
