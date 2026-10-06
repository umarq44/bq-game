/* March of the Black Queen, Knight of Lodis style: Prototype 1
 * One isometric map with height, 3 vs 3 units, move and attack,
 * simple enemy AI, and the chapter grade screen (turns + units lost).
 * Plain C, Mode 3, no libraries. Placeholder graphics.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#ifdef HOST_TEST
static u16 fake_vram[240 * 160];
static u16 fake_keys = 0x03FF;
#define VRAM fake_vram
#define REG_DISPCNT fake_dispcnt
static u16 fake_dispcnt;
#define REG_KEYINPUT fake_keys
static u16 fake_vcount = 0;
#define REG_VCOUNT fake_vcount
#else
#define VRAM ((volatile u16 *)0x06000000)
#define REG_DISPCNT (*(volatile u16 *)0x04000000)
#define REG_VCOUNT (*(volatile u16 *)0x04000006)
#define REG_KEYINPUT (*(volatile u16 *)0x04000130)
#endif

#define RGB(r, g, b) ((u16)((r) | ((g) << 5) | ((b) << 10)))

#define KEY_A 1
#define KEY_B 2
#define KEY_START 8
#define KEY_RIGHT 16
#define KEY_LEFT 32
#define KEY_UP 64
#define KEY_DOWN 128

/* ---------- drawing ---------- */

static void px(int x, int y, u16 c) {
    if ((unsigned)x < 240 && (unsigned)y < 160) VRAM[y * 240 + x] = c;
}

static void hline(int x0, int x1, int y, u16 c) {
    int x;
    for (x = x0; x <= x1; x++) px(x, y, c);
}

static void rect(int x, int y, int w, int h, u16 c) {
    int j;
    for (j = 0; j < h; j++) hline(x, x + w - 1, y + j, c);
}

static void clear(u16 c) {
    int i;
    for (i = 0; i < 240 * 160; i++) VRAM[i] = c;
}

/* 3x5 font: digits 0-9, letters A-Z, then + - : ! and space */
static const u8 FONT[41][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},
    {7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},
    {5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},
    {6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
    {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},
    {7,1,2,4,7},
    {0,2,7,2,0},{0,0,7,0,0},{0,2,0,2,0},{2,2,2,0,2},{0,0,0,0,0}
};

static int glyph_index(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
    if (ch == '+') return 36;
    if (ch == '-') return 37;
    if (ch == ':') return 38;
    if (ch == '!') return 39;
    return 40;
}

static void text(int x, int y, const char *s, u16 c, int scale) {
    while (*s) {
        int g = glyph_index(*s);
        int row, col;
        for (row = 0; row < 5; row++)
            for (col = 0; col < 3; col++)
                if (FONT[g][row] & (4 >> col))
                    rect(x + col * scale, y + row * scale, scale, scale, c);
        x += 4 * scale;
        s++;
    }
}

static char *num_to_str(int n, char *buf) {
    /* writes a non-negative number, returns buf */
    char tmp[12];
    int i = 0, j = 0;
    if (n < 0) n = 0;
    if (n == 0) tmp[i++] = '0';
    while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

/* ---------- game data ---------- */

#define MW 8
#define MH 8
#define PAR_TURNS 6
#define MOVE_RANGE 3

static const u8 HEIGHT[MH][MW] = {
    {0,0,0,1,1,0,0,0},
    {0,0,1,1,1,1,0,0},
    {0,1,1,2,2,1,0,0},
    {0,1,2,2,2,1,1,0},
    {0,0,1,2,2,1,1,0},
    {0,0,1,1,1,1,0,0},
    {0,0,0,1,1,0,0,0},
    {0,0,0,0,0,0,0,0}
};

typedef struct {
    int x, y, hp, team, alive, done;
} Unit;

#define NUM_UNITS 6
static Unit U[NUM_UNITS];

enum { S_FREE, S_MOVE, S_ACT, S_END };

static int state;
static int cur_x, cur_y;
static int sel;
static int sel_old_x, sel_old_y;
static int turn, lost, victory;
static int reach[MH][MW];

static const char *GRADE_NAME[14] = {
    "S+","S","A+","A","A-","B+","B","B-","C+","C","C-","D+","D","D-"
};
static const int GRADE_MIN[14] = {98,94,90,86,82,78,74,66,60,54,48,40,30,0};

static int iabs(int a) { return a < 0 ? -a : a; }

static int unit_at(int x, int y) {
    int i;
    for (i = 0; i < NUM_UNITS; i++)
        if (U[i].alive && U[i].x == x && U[i].y == y) return i;
    return -1;
}

static void init_game(void) {
    int i;
    static const int sx[NUM_UNITS] = {0, 2, 0, 7, 5, 7};
    static const int sy[NUM_UNITS] = {0, 0, 2, 7, 7, 5};
    for (i = 0; i < NUM_UNITS; i++) {
        U[i].x = sx[i];
        U[i].y = sy[i];
        U[i].team = i < 3 ? 0 : 1;
        U[i].hp = i < 3 ? 12 : 9;
        U[i].alive = 1;
        U[i].done = 0;
    }
    state = S_FREE;
    cur_x = 0;
    cur_y = 0;
    sel = -1;
    turn = 1;
    lost = 0;
    victory = 0;
}

static void compute_reach(int ui) {
    int d[MH][MW];
    int x, y, step;
    for (y = 0; y < MH; y++)
        for (x = 0; x < MW; x++) { d[y][x] = 99; reach[y][x] = 0; }
    d[U[ui].y][U[ui].x] = 0;
    for (step = 0; step < MOVE_RANGE; step++) {
        for (y = 0; y < MH; y++) {
            for (x = 0; x < MW; x++) {
                int k;
                static const int dx[4] = {1, -1, 0, 0};
                static const int dy[4] = {0, 0, 1, -1};
                if (d[y][x] != step) continue;
                for (k = 0; k < 4; k++) {
                    int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= MW || ny >= MH) continue;
                    if (iabs(HEIGHT[ny][nx] - HEIGHT[y][x]) > 1) continue;
                    if (unit_at(nx, ny) >= 0) continue;
                    if (d[ny][nx] > step + 1) d[ny][nx] = step + 1;
                }
            }
        }
    }
    for (y = 0; y < MH; y++)
        for (x = 0; x < MW; x++)
            if (d[y][x] <= MOVE_RANGE) reach[y][x] = 1;
}

static int count_alive(int team) {
    int i, n = 0;
    for (i = 0; i < NUM_UNITS; i++)
        if (U[i].alive && U[i].team == team) n++;
    return n;
}

static int adjacent_enemy(int ui) {
    int i;
    for (i = 0; i < NUM_UNITS; i++)
        if (U[i].alive && U[i].team != U[ui].team &&
            iabs(U[i].x - U[ui].x) + iabs(U[i].y - U[ui].y) == 1)
            return i;
    return -1;
}

static void check_end(void) {
    if (count_alive(1) == 0) { victory = 1; state = S_END; }
    else if (count_alive(0) == 0) { victory = 0; state = S_END; }
}

static void do_attack(int a, int t) {
    int dmg = 5;
    if (HEIGHT[U[a].y][U[a].x] > HEIGHT[U[t].y][U[t].x]) dmg += 2;
    U[t].hp -= dmg;
    if (U[t].hp <= 0) {
        U[t].alive = 0;
        if (U[t].team == 0) lost++;
    }
}

static void enemy_turn(void) {
    int i;
    for (i = 3; i < NUM_UNITS; i++) {
        int step;
        if (!U[i].alive) continue;
        for (step = 0; step < MOVE_RANGE; step++) {
            int best = -1, bestd = 999, k;
            static const int dx[4] = {1, -1, 0, 0};
            static const int dy[4] = {0, 0, 1, -1};
            int cx = U[i].x, cy = U[i].y;
            int p;
            int target = -1, td = 999;
            if (adjacent_enemy(i) >= 0) break;
            for (p = 0; p < 3; p++) {
                int dd;
                if (!U[p].alive) continue;
                dd = iabs(U[p].x - cx) + iabs(U[p].y - cy);
                if (dd < td) { td = dd; target = p; }
            }
            if (target < 0) break;
            for (k = 0; k < 4; k++) {
                int nx = cx + dx[k], ny = cy + dy[k], dd;
                if (nx < 0 || ny < 0 || nx >= MW || ny >= MH) continue;
                if (iabs(HEIGHT[ny][nx] - HEIGHT[cy][cx]) > 1) continue;
                if (unit_at(nx, ny) >= 0) continue;
                dd = iabs(U[target].x - nx) + iabs(U[target].y - ny);
                if (dd < bestd) { bestd = dd; best = k; }
            }
            if (best < 0 || bestd >= td) break;
            U[i].x = cx + dx[best];
            U[i].y = cy + dy[best];
        }
        {
            int t = adjacent_enemy(i);
            if (t >= 0) do_attack(i, t);
        }
        check_end();
        if (state == S_END) return;
    }
    turn++;
    for (i = 0; i < 3; i++) U[i].done = 0;
}

static int compute_score(void) {
    int s = 100;
    if (turn > PAR_TURNS) s -= 3 * (turn - PAR_TURNS);
    s -= 12 * lost;
    if (s < 0) s = 0;
    return s;
}

static int grade_index(int score) {
    int g;
    for (g = 0; g < 14; g++)
        if (score >= GRADE_MIN[g]) return g;
    return 13;
}

/* ---------- rendering ---------- */

#define TW 12   /* half tile width  */
#define TH 6    /* half tile height */
#define OX 120
#define OY 44

static void tile_top(int cx, int cy, u16 c) {
    int j, half;
    for (j = 0; j < 12; j++) {
        half = j < 6 ? 2 * j + 2 : 2 * (11 - j) + 2;
        hline(cx - half, cx + half - 1, cy - TH + j, c);
    }
}

static void tile_sides(int cx, int cy, int h) {
    int i, y, bottom = cy + 6 + (h + 1) * 6;
    for (i = 0; i < 12; i++) {
        int ytop = cy + i / 2;
        for (y = ytop; y < bottom - (11 - i) / 2 - 0; y++) px(cx - 12 + i, y, RGB(10, 7, 4));
    }
    for (i = 0; i < 12; i++) {
        int ytop = cy + 6 - i / 2;
        for (y = ytop; y < bottom - i / 2 - 0; y++) px(cx + i, y, RGB(7, 5, 3));
    }
}

static void tile_outline(int cx, int cy, u16 c) {
    int j, half;
    for (j = 0; j < 12; j++) {
        half = j < 6 ? 2 * j + 2 : 2 * (11 - j) + 2;
        px(cx - half, cy - TH + j, c);
        px(cx + half - 1, cy - TH + j, c);
    }
    hline(cx - 2, cx + 1, cy - TH, c);
    hline(cx - 2, cx + 1, cy + TH - 1, c);
}

static void draw_unit(int ui, int cx, int cy) {
    u16 body, edge = RGB(2, 2, 2);
    Unit *u = &U[ui];
    if (u->team == 0) body = u->done ? RGB(12, 12, 14) : RGB(6, 12, 30);
    else body = RGB(28, 6, 6);
    rect(cx - 3, cy - 10, 7, 11, edge);
    rect(cx - 2, cy - 9, 5, 9, body);
    rect(cx - 2, cy - 12, 5, 3, RGB(28, 22, 16));
    /* tiny hp bar */
    {
        int w = (u->hp * 7) / (u->team == 0 ? 12 : 9);
        if (w < 1) w = 1;
        rect(cx - 3, cy + 2, 7, 1, RGB(6, 2, 2));
        rect(cx - 3, cy + 2, w, 1, RGB(6, 28, 8));
    }
}

static void draw_map(void) {
    int d, x, y;
    for (d = 0; d <= MW + MH - 2; d++) {
        for (x = 0; x < MW; x++) {
            int h, cx, cy, u;
            u16 top;
            y = d - x;
            if (y < 0 || y >= MH) continue;
            h = HEIGHT[y][x];
            cx = OX + (x - y) * TW;
            cy = OY + (x + y) * TH - h * 6;
            tile_sides(cx, cy, h);
            top = h == 0 ? RGB(8, 20, 8) : (h == 1 ? RGB(13, 24, 10) : RGB(19, 26, 13));
            if (state == S_MOVE && reach[y][x]) top = RGB(10, 16, 28);
            if (state == S_ACT && sel >= 0 &&
                iabs(x - U[sel].x) + iabs(y - U[sel].y) == 1 &&
                unit_at(x, y) >= 0 && U[unit_at(x, y)].team == 1)
                top = RGB(28, 10, 8);
            tile_top(cx, cy, top);
            if (x == cur_x && y == cur_y) tile_outline(cx, cy, RGB(31, 31, 4));
            u = unit_at(x, y);
            if (u >= 0) draw_unit(u, cx, cy);
        }
    }
}

static void draw_hud(void) {
    char buf[16];
    text(4, 4, "TURN", RGB(31, 31, 31), 1);
    text(24, 4, num_to_str(turn, buf), RGB(31, 31, 31), 1);
    text(4, 11, "LOST", RGB(31, 31, 31), 1);
    text(24, 11, num_to_str(lost, buf), RGB(31, 31, 31), 1);
    text(4, 18, "PAR", RGB(20, 20, 20), 1);
    text(20, 18, num_to_str(PAR_TURNS, buf), RGB(20, 20, 20), 1);
    {
        int u = unit_at(cur_x, cur_y);
        if (u >= 0) {
            text(180, 4, u < 3 ? "ALLY" : "FOE", RGB(31, 31, 31), 1);
            text(180, 11, "HP", RGB(31, 31, 31), 1);
            text(192, 11, num_to_str(U[u].hp, buf), RGB(31, 31, 31), 1);
        }
        text(180, 18, "H", RGB(31, 31, 31), 1);
        text(188, 18, num_to_str(HEIGHT[cur_y][cur_x], buf), RGB(31, 31, 31), 1);
    }
    if (state == S_FREE) text(4, 150, "A: SELECT  START: END TURN", RGB(31, 31, 31), 1);
    if (state == S_MOVE) text(4, 150, "A: MOVE HERE  B: CANCEL", RGB(31, 31, 31), 1);
    if (state == S_ACT) text(4, 150, "A: ATTACK OR WAIT  B: WAIT", RGB(31, 31, 31), 1);
}

static void draw_end(void) {
    char buf[16];
    int score = compute_score();
    int g = grade_index(victory ? score : 0);
    if (!victory) g = 13;
    clear(RGB(2, 2, 6));
    text(60, 12, victory ? "CHAPTER 1 CLEAR" : "DEFEAT", RGB(31, 31, 20), 2);
    text(70, 40, "TURNS", RGB(31, 31, 31), 1);
    text(110, 40, num_to_str(turn, buf), RGB(31, 31, 31), 1);
    text(70, 50, "UNITS LOST", RGB(31, 31, 31), 1);
    text(130, 50, num_to_str(lost, buf), RGB(31, 31, 31), 1);
    text(70, 60, "SCORE", RGB(31, 31, 31), 1);
    text(110, 60, num_to_str(victory ? score : 0, buf), RGB(31, 31, 31), 1);
    text(90, 80, "GRADE", RGB(20, 24, 31), 2);
    text(100, 96, GRADE_NAME[g], RGB(31, 24, 4), 6);
    text(70, 140, "PRESS A TO RETRY", RGB(31, 31, 31), 1);
}

static void redraw(void) {
    if (state == S_END) { draw_end(); return; }
    clear(RGB(3, 4, 10));
    draw_map();
    draw_hud();
}

/* ---------- main loop ---------- */

static void move_cursor(int dx, int dy) {
    int nx = cur_x + dx, ny = cur_y + dy;
    if (nx < 0 || ny < 0 || nx >= MW || ny >= MH) return;
    cur_x = nx;
    cur_y = ny;
}

static void finish_unit(void) {
    if (sel >= 0) U[sel].done = 1;
    sel = -1;
    state = S_FREE;
    check_end();
}

static void all_done_check(void) {
    int i, all = 1;
    for (i = 0; i < 3; i++)
        if (U[i].alive && !U[i].done) all = 0;
    if (all && state == S_FREE) enemy_turn();
}

static void update(u16 pressed) {
    if (state == S_END) {
        if (pressed & KEY_A) init_game();
        return;
    }
    if (pressed & KEY_RIGHT) move_cursor(1, 0);
    if (pressed & KEY_LEFT) move_cursor(-1, 0);
    if (pressed & KEY_DOWN) move_cursor(0, 1);
    if (pressed & KEY_UP) move_cursor(0, -1);

    if (state == S_FREE) {
        if (pressed & KEY_A) {
            int u = unit_at(cur_x, cur_y);
            if (u >= 0 && u < 3 && !U[u].done) {
                sel = u;
                sel_old_x = U[u].x;
                sel_old_y = U[u].y;
                compute_reach(u);
                state = S_MOVE;
            }
        }
        if (pressed & KEY_START) {
            enemy_turn();
        }
    } else if (state == S_MOVE) {
        if (pressed & KEY_B) {
            sel = -1;
            state = S_FREE;
        } else if (pressed & KEY_A) {
            if (reach[cur_y][cur_x] && (unit_at(cur_x, cur_y) < 0 || unit_at(cur_x, cur_y) == sel)) {
                U[sel].x = cur_x;
                U[sel].y = cur_y;
                if (adjacent_enemy(sel) >= 0) state = S_ACT;
                else finish_unit();
            }
        }
    } else if (state == S_ACT) {
        if (pressed & KEY_B) {
            finish_unit();
        } else if (pressed & KEY_A) {
            int t = unit_at(cur_x, cur_y);
            if (t >= 0 && U[t].team == 1 &&
                iabs(U[t].x - U[sel].x) + iabs(U[t].y - U[sel].y) == 1) {
                do_attack(sel, t);
            }
            finish_unit();
        }
    }
    if (state != S_END) all_done_check();
    check_end();
}

int main(void) {
    u16 prev = 0;
    REG_DISPCNT = 3 | (1 << 10);
    init_game();
    redraw();
    for (;;) {
        u16 down, pressed;
        while (REG_VCOUNT >= 160) {}
        while (REG_VCOUNT < 160) {}
        down = (u16)(~REG_KEYINPUT) & 0x03FF;
        pressed = down & (u16)~prev;
        prev = down;
        if (pressed) {
            update(pressed);
            redraw();
        }
#ifdef HOST_TEST
        break;
#endif
    }
    return 0;
}
