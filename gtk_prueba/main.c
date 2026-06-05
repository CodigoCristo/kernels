/*
 * sprite-bench: GTK4 + Adwaita — benchmark de física vectorizada
 *
 * Muestra tiempos separados:
 *   • Física  (µs) — el hot path que V4 vectoriza con AVX-512
 *   • Render  (µs) — Cairo software rendering (cuello de botella independiente)
 *   • FPS total
 *   • CPU %
 *
 * Modo headless (--headless [N_SPRITES] [SECONDS]):
 *   Corre SOLO la física sin dibujar nada.
 *   Mide millones de updates/segundo para comparar base vs v4 sin ruido de Cairo.
 *
 * Meson:
 *   meson setup build-base -Dvector_level=base  && meson compile -C build-base
 *   meson setup build-v4   -Dvector_level=v4    && meson compile -C build-v4
 *
 *   ./build-base/sprite-bench --headless 2000 5
 *   ./build-v4/sprite-bench   --headless 2000 5
 */

#include <adwaita.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ─── Configuración ─────────────────────────────────────────────── */
#define MAX_SPRITES      5000
#define DEFAULT_SPRITES   500
#define SPRITE_SIZE        48
#define TARGET_FPS         60
#define CPU_SAMPLE_MS     400
#define SMOOTH_ALPHA     0.12   /* EMA para suavizar µs */

static const guint32 PALETTE[] = {
    0xFF5C6BC0, 0xFFEC407A, 0xFF26C6DA, 0xFF66BB6A,
    0xFFFFCA28, 0xFFFF7043, 0xFFAB47BC, 0xFF29B6F6,
    0xFF9CCC65, 0xFFFF8A65, 0xFF26A69A, 0xFFEF5350,
};
#define PALETTE_SIZE (sizeof(PALETTE)/sizeof(PALETTE[0]))

/* ─── Sprite ─────────────────────────────────────────────────────── */
typedef struct {
    float x, y;
    float vx, vy;
    float angle, spin;
    float scale;
    int   color_idx;
    int   shape;
} Sprite;

/* ─── AppData ────────────────────────────────────────────────────── */
typedef struct {
    /* widgets */
    GtkWidget *drawing_area;
    GtkWidget *lbl_fps,  *lbl_cpu;
    GtkWidget *lbl_phys, *lbl_render;   /* ← nuevos contadores */
    GtkWidget *lbl_sprites, *lbl_build;
    GtkWidget *scale_sprites, *btn_toggle;
    GtkWidget *progress_fps, *progress_cpu;
    GtkWidget *progress_phys, *progress_render;

    /* estado */
    Sprite  sprites[MAX_SPRITES];
    int     n_sprites;
    int     running;

    /* métricas de tiempo */
    double  fps;
    double  cpu_usage;
    double  phys_us;     /* tiempo de física suavizado (EMA) */
    double  render_us;   /* tiempo de render suavizado (EMA) */

    gint64  last_frame_us;
    gint64  frame_count;
    gint64  fps_accum_us;

    /* CPU sampling */
    gint64  last_cpu_time;
    gint64  last_wall_us;
    guint   cpu_timer;

    int     canvas_w, canvas_h;

    cairo_surface_t *sprite_surfaces[PALETTE_SIZE * 4];
} AppData;

/* ══════════════════════════════════════════════════════════════════
 *  LECTURA DE CPU
 * ══════════════════════════════════════════════════════════════════ */
static gint64 read_proc_cpu_ticks(void) {
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f) return 0;
    gint64 utime = 0, stime = 0;
    char buf[512];
    if (fgets(buf, sizeof(buf), f)) {
        char *p = buf; int field = 0;
        while (*p && field < 13) { if (*p == ' ') field++; p++; }
        sscanf(p, "%" G_GINT64_FORMAT " %" G_GINT64_FORMAT, &utime, &stime);
    }
    fclose(f);
    return utime + stime;
}

static gboolean update_cpu(gpointer user_data) {
    AppData *app = user_data;
    gint64 now_us  = g_get_monotonic_time();
    gint64 now_cpu = read_proc_cpu_ticks();
    gint64 d_wall  = now_us  - app->last_wall_us;
    gint64 d_cpu   = now_cpu - app->last_cpu_time;
    long   hz      = sysconf(_SC_CLK_TCK);
    if (d_wall > 0 && hz > 0) {
        app->cpu_usage = ((double)d_cpu / hz) / ((double)d_wall / 1e6) * 100.0;
        if (app->cpu_usage > 100.0) app->cpu_usage = 100.0;
    }
    app->last_wall_us  = now_us;
    app->last_cpu_time = now_cpu;
    return G_SOURCE_CONTINUE;
}

/* ══════════════════════════════════════════════════════════════════
 *  HOT PATH — FÍSICA
 *  Con -march=x86-64-v4 + -O3 + -ffast-math el compilador
 *  vectoriza este bucle usando registros ZMM (AVX-512, 16 floats/op).
 *  Con -O2 sin march usa SSE2 escalar (1 float/op).
 * ══════════════════════════════════════════════════════════════════ */
static void __attribute__((optimize("O3")))
update_sprites(Sprite *s, int n, float dt, float W, float H) {
    const float sz = (float)SPRITE_SIZE;
    for (int i = 0; i < n; i++) {
        s[i].x     += s[i].vx   * dt;
        s[i].y     += s[i].vy   * dt;
        s[i].angle += s[i].spin * dt;

        if      (s[i].x < 0.0f)    { s[i].x  = 0.0f;    s[i].vx =  fabsf(s[i].vx); }
        else if (s[i].x > W - sz)  { s[i].x  = W - sz;  s[i].vx = -fabsf(s[i].vx); }
        if      (s[i].y < 0.0f)    { s[i].y  = 0.0f;    s[i].vy =  fabsf(s[i].vy); }
        else if (s[i].y > H - sz)  { s[i].y  = H - sz;  s[i].vy = -fabsf(s[i].vy); }
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  MODO HEADLESS
 *  Corre SOLO la física sin ningún rendering ni GTK.
 *  Uso: ./sprite-bench --headless [n_sprites] [segundos]
 * ══════════════════════════════════════════════════════════════════ */
static int run_headless(int n_sprites, int seconds) {
    Sprite *sprites = calloc(n_sprites, sizeof(Sprite));
    srand(42);
    for (int i = 0; i < n_sprites; i++) {
        sprites[i].x     = (float)(rand() % 1920);
        sprites[i].y     = (float)(rand() % 1080);
        sprites[i].vx    = ((float)rand()/RAND_MAX * 400.f - 200.f);
        sprites[i].vy    = ((float)rand()/RAND_MAX * 400.f - 200.f);
        sprites[i].spin  = ((float)rand()/RAND_MAX * 4.f   - 2.f);
        sprites[i].angle = 0.f;
        sprites[i].scale = 1.f;
    }

    /* detectar nivel SIMD en tiempo de compilación */
#ifdef __AVX512F__
    const char *simd = "AVX-512 (x86-64-v4)";
#elif defined(__AVX2__)
    const char *simd = "AVX2    (x86-64-v3)";
#else
    const char *simd = "SSE2    (base)     ";
#endif

    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║        SPRITE-BENCH  —  MODO HEADLESS        ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("  SIMD detectado : %s\n", simd);
    printf("  Sprites        : %d\n", n_sprites);
    printf("  Duración       : %d s\n\n", seconds);
    fflush(stdout);

    const float dt     = 1.0f / 60.0f;
    const float W      = 1920.f, H = 1080.f;
    long long   frames = 0;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    struct timespec deadline = t0;
    deadline.tv_sec += seconds;

    /* bucle puro de física */
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (t1.tv_sec > deadline.tv_sec ||
           (t1.tv_sec == deadline.tv_sec && t1.tv_nsec >= deadline.tv_nsec))
            break;
        update_sprites(sprites, n_sprites, dt, W, H);
        frames++;
    }

    double elapsed = (t1.tv_sec - t0.tv_sec) +
                     (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    double updates_per_sec   = (double)frames / elapsed;
    double ns_per_frame      = elapsed / frames * 1e9;
    double million_sprites_s = updates_per_sec * n_sprites / 1e6;

    printf("  Frames totales : %lld\n",   frames);
    printf("  Tiempo real    : %.3f s\n", elapsed);
    printf("  Updates/seg    : %.0f\n",   updates_per_sec);
    printf("  ns/frame       : %.1f ns\n",ns_per_frame);
    printf("  Sprites·M/s    : %.1f M sprites/s\n\n", million_sprites_s);
    printf("  (compara esta cifra entre build-base y build-v4)\n\n");

    free(sprites);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  SPRITES — generación de superficies Cairo (se hace UNA sola vez)
 * ══════════════════════════════════════════════════════════════════ */
static void draw_star(cairo_t *cr, double cx, double cy, double r) {
    const double inner = r * 0.4;
    for (int i = 0; i < 10; i++) {
        double a  = (i * G_PI / 5.0) - G_PI / 2.0;
        double rr = (i % 2 == 0) ? r : inner;
        double x  = cx + cos(a) * rr;
        double y  = cy + sin(a) * rr;
        if (i == 0) cairo_move_to(cr, x, y);
        else        cairo_line_to(cr, x, y);
    }
    cairo_close_path(cr);
}

static cairo_surface_t *make_sprite_surface(guint32 argb, int shape) {
    int sz = SPRITE_SIZE;
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sz, sz);
    cairo_t *cr = cairo_create(surf);
    double R = ((argb >> 16) & 0xFF) / 255.0;
    double G = ((argb >>  8) & 0xFF) / 255.0;
    double B = ((argb >>  0) & 0xFF) / 255.0;
    double pad = 4.0;

    /* sombra */
    cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
    switch (shape) {
    case 0: cairo_arc(cr, sz/2.0+1, sz/2.0+2, sz/2.0-pad-1, 0, 2*G_PI); cairo_fill(cr); break;
    case 1: draw_star(cr, sz/2.0+1, sz/2.0+2, sz/2.0-pad-1); cairo_fill(cr); break;
    case 2: {
        double x0=pad+1,y0=pad+2,w=sz-2*pad-2,h=w,ra=8;
        cairo_move_to(cr,x0+ra,y0); cairo_line_to(cr,x0+w-ra,y0);
        cairo_arc(cr,x0+w-ra,y0+ra,ra,-G_PI/2,0);
        cairo_line_to(cr,x0+w,y0+h-ra); cairo_arc(cr,x0+w-ra,y0+h-ra,ra,0,G_PI/2);
        cairo_line_to(cr,x0+ra,y0+h); cairo_arc(cr,x0+ra,y0+h-ra,ra,G_PI/2,G_PI);
        cairo_line_to(cr,x0,y0+ra); cairo_arc(cr,x0+ra,y0+ra,ra,G_PI,3*G_PI/2);
        cairo_close_path(cr); cairo_fill(cr); break; }
    case 3:
        cairo_move_to(cr,sz/2.0+1,pad+2);
        cairo_line_to(cr,sz-pad+1,sz-pad+2);
        cairo_line_to(cr,pad+1,sz-pad+2);
        cairo_close_path(cr); cairo_fill(cr); break;
    }

    /* color */
    cairo_set_source_rgba(cr, R, G, B, 1.0);
    switch (shape) {
    case 0: cairo_arc(cr, sz/2.0, sz/2.0, sz/2.0-pad, 0, 2*G_PI); cairo_fill(cr);
        { cairo_pattern_t *g = cairo_pattern_create_radial(sz*.35,sz*.3,1,sz/2.,sz/2.,sz/2.-pad);
          cairo_pattern_add_color_stop_rgba(g,0,1,1,1,.5);
          cairo_pattern_add_color_stop_rgba(g,.6,R,G,B,0);
          cairo_set_source(cr,g);
          cairo_arc(cr,sz/2.,sz/2.,sz/2.-pad,0,2*G_PI); cairo_fill(cr);
          cairo_pattern_destroy(g); }
        break;
    case 1: draw_star(cr, sz/2.0, sz/2.0, sz/2.0-pad); cairo_fill(cr); break;
    case 2: {
        double x0=pad,y0=pad,w=sz-2*pad-2,h=w,ra=8;
        cairo_move_to(cr,x0+ra,y0); cairo_line_to(cr,x0+w-ra,y0);
        cairo_arc(cr,x0+w-ra,y0+ra,ra,-G_PI/2,0);
        cairo_line_to(cr,x0+w,y0+h-ra); cairo_arc(cr,x0+w-ra,y0+h-ra,ra,0,G_PI/2);
        cairo_line_to(cr,x0+ra,y0+h); cairo_arc(cr,x0+ra,y0+h-ra,ra,G_PI/2,G_PI);
        cairo_line_to(cr,x0,y0+ra); cairo_arc(cr,x0+ra,y0+ra,ra,G_PI,3*G_PI/2);
        cairo_close_path(cr); cairo_fill(cr); break; }
    case 3:
        cairo_move_to(cr,sz/2.0,pad);
        cairo_line_to(cr,sz-pad,sz-pad);
        cairo_line_to(cr,pad,sz-pad);
        cairo_close_path(cr); cairo_fill(cr); break;
    }
    cairo_destroy(cr);
    return surf;
}

/* ══════════════════════════════════════════════════════════════════
 *  INIT SPRITES
 * ══════════════════════════════════════════════════════════════════ */
static void init_sprites(AppData *app) {
    for (int i = 0; i < MAX_SPRITES; i++) {
        app->sprites[i].x         = (float)(rand() % MAX(app->canvas_w - SPRITE_SIZE, 1));
        app->sprites[i].y         = (float)(rand() % MAX(app->canvas_h - SPRITE_SIZE, 1));
        app->sprites[i].vx        = ((float)rand()/RAND_MAX * 400.f - 200.f);
        app->sprites[i].vy        = ((float)rand()/RAND_MAX * 400.f - 200.f);
        app->sprites[i].angle     = (float)rand()/RAND_MAX * 2.f * (float)G_PI;
        app->sprites[i].spin      = ((float)rand()/RAND_MAX * 4.f - 2.f);
        app->sprites[i].scale     = 0.6f + (float)rand()/RAND_MAX * 0.9f;
        app->sprites[i].color_idx = rand() % PALETTE_SIZE;
        app->sprites[i].shape     = rand() % 4;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  DRAW — mide su propio tiempo
 * ══════════════════════════════════════════════════════════════════ */
static void on_draw(GtkDrawingArea *da, cairo_t *cr,
                    int w, int h, gpointer user_data) {
    (void)da;
    AppData *app = user_data;
    app->canvas_w = w;
    app->canvas_h = h;

    gint64 t0 = g_get_monotonic_time();

    cairo_set_source_rgb(cr, 0.09, 0.09, 0.11);
    cairo_paint(cr);

    for (int i = 0; i < app->n_sprites; i++) {
        Sprite *s = &app->sprites[i];
        cairo_surface_t *surf = app->sprite_surfaces[s->color_idx * 4 + s->shape];
        cairo_save(cr);
        cairo_translate(cr,
            s->x + SPRITE_SIZE * s->scale * 0.5,
            s->y + SPRITE_SIZE * s->scale * 0.5);
        cairo_rotate(cr, s->angle);
        cairo_scale(cr, s->scale, s->scale);
        cairo_translate(cr, -SPRITE_SIZE * 0.5, -SPRITE_SIZE * 0.5);
        cairo_set_source_surface(cr, surf, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    gint64 t1 = g_get_monotonic_time();
    double raw = (double)(t1 - t0);
    /* EMA suavizado */
    app->render_us = app->render_us * (1.0 - SMOOTH_ALPHA) + raw * SMOOTH_ALPHA;
}

/* ══════════════════════════════════════════════════════════════════
 *  TICK — mide tiempo de física por separado
 * ══════════════════════════════════════════════════════════════════ */
static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock,
                        gpointer user_data) {
    AppData *app = user_data;
    if (!app->running) return G_SOURCE_CONTINUE;

    gint64 now_us = gdk_frame_clock_get_frame_time(clock);
    gint64 delta  = now_us - app->last_frame_us;
    if (delta <= 0) delta = 16667;
    app->last_frame_us = now_us;

    float dt = (float)delta / 1e6f;
    if (dt > 0.1f) dt = 0.1f;

    /* ── medir SOLO la física ── */
    gint64 tp0 = g_get_monotonic_time();
    update_sprites(app->sprites, app->n_sprites, dt,
                   (float)app->canvas_w, (float)app->canvas_h);
    gint64 tp1 = g_get_monotonic_time();
    double raw_phys = (double)(tp1 - tp0);
    app->phys_us = app->phys_us * (1.0 - SMOOTH_ALPHA) + raw_phys * SMOOTH_ALPHA;

    /* FPS (ventana de 45 frames) */
    app->fps_accum_us += delta;
    app->frame_count++;
    if (app->frame_count >= 45) {
        app->fps = 45.0 * 1e6 / (double)app->fps_accum_us;
        app->fps_accum_us = 0;
        app->frame_count  = 0;

        /* actualizar UI */
        char buf[80];

        snprintf(buf, sizeof(buf), "%.1f FPS", app->fps);
        gtk_label_set_text(GTK_LABEL(app->lbl_fps), buf);

        snprintf(buf, sizeof(buf), "CPU %.1f%%", app->cpu_usage);
        gtk_label_set_text(GTK_LABEL(app->lbl_cpu), buf);

        /* física: mostrar µs si > 1000, si no ns */
        if (app->phys_us >= 1000.0)
            snprintf(buf, sizeof(buf), "%.2f ms", app->phys_us / 1000.0);
        else
            snprintf(buf, sizeof(buf), "%.1f µs", app->phys_us);
        gtk_label_set_text(GTK_LABEL(app->lbl_phys), buf);

        /* render siempre en ms */
        snprintf(buf, sizeof(buf), "%.2f ms", app->render_us / 1000.0);
        gtk_label_set_text(GTK_LABEL(app->lbl_render), buf);

        /* barras */
        double fps_norm = app->fps / TARGET_FPS;
        if (fps_norm > 1.0) fps_norm = 1.0;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_fps), fps_norm);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_cpu),
                                      app->cpu_usage / 100.0);

        /* física: barra respecto a 1 frame (16.67ms) */
        double phys_norm = app->phys_us / 16667.0;
        if (phys_norm > 1.0) phys_norm = 1.0;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_phys), phys_norm);

        /* render: barra respecto a 1 frame */
        double render_norm = app->render_us / 16667.0;
        if (render_norm > 1.0) render_norm = 1.0;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_render), render_norm);
    }

    gtk_widget_queue_draw(app->drawing_area);
    return G_SOURCE_CONTINUE;
}

/* ══════════════════════════════════════════════════════════════════
 *  CALLBACKS UI
 * ══════════════════════════════════════════════════════════════════ */
static void on_sprites_changed(GtkRange *range, gpointer user_data) {
    AppData *app = user_data;
    app->n_sprites = (int)gtk_range_get_value(range);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d sprites", app->n_sprites);
    gtk_label_set_text(GTK_LABEL(app->lbl_sprites), buf);
}

static void on_toggle(GtkButton *btn, gpointer user_data) {
    AppData *app = user_data;
    app->running = !app->running;
    gtk_button_set_label(btn, app->running ? "⏸ Pausar" : "▶ Reanudar");
}

/* ══════════════════════════════════════════════════════════════════
 *  HELPERS: fila métrica con barra
 * ══════════════════════════════════════════════════════════════════ */
static void make_metric_row(GtkBox *parent,
                             const char *title_str,
                             const char *init_val,
                             GtkWidget **out_label,
                             GtkWidget **out_bar,
                             const char *bar_css_class) {
    GtkBox *row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 3));

    GtkBox *header = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    GtkLabel *title = GTK_LABEL(gtk_label_new(title_str));
    gtk_widget_add_css_class(GTK_WIDGET(title), "caption");
    gtk_widget_set_hexpand(GTK_WIDGET(title), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(title), GTK_ALIGN_START);

    *out_label = gtk_label_new(init_val);
    gtk_widget_add_css_class(*out_label, "title-4");

    gtk_box_append(header, GTK_WIDGET(title));
    gtk_box_append(header, *out_label);

    *out_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(*out_bar), 0.0);
    if (bar_css_class)
        gtk_widget_add_css_class(*out_bar, bar_css_class);

    gtk_box_append(row, GTK_WIDGET(header));
    gtk_box_append(row, *out_bar);
    gtk_box_append(parent, GTK_WIDGET(row));
}

/* ══════════════════════════════════════════════════════════════════
 *  BUILD UI
 * ══════════════════════════════════════════════════════════════════ */
static void build_ui(GtkApplication *gapp, gpointer user_data) {
    AppData *app = user_data;

    for (int c = 0; c < (int)PALETTE_SIZE; c++)
        for (int s = 0; s < 4; s++)
            app->sprite_surfaces[c * 4 + s] = make_sprite_surface(PALETTE[c], s);

    AdwApplicationWindow *win =
        ADW_APPLICATION_WINDOW(adw_application_window_new(gapp));
    gtk_window_set_title(GTK_WINDOW(win), "Sprite Bench — x86-64-v4");
    gtk_window_set_default_size(GTK_WINDOW(win), 1160, 740);

    AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    AdwHeaderBar   *header       = ADW_HEADER_BAR(adw_header_bar_new());
    adw_header_bar_set_title_widget(header,
        adw_window_title_new("🚀 Sprite Bench", "GTK4 + Adwaita — v4 Optimization"));
    adw_toolbar_view_add_top_bar(toolbar_view, GTK_WIDGET(header));

    GtkBox *hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));

    /* ── canvas ── */
    app->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(app->drawing_area, TRUE);
    gtk_widget_set_vexpand(app->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->drawing_area),
                                   on_draw, app, NULL);
    gtk_widget_add_tick_callback(app->drawing_area, on_tick, app, NULL);
    gtk_box_append(hbox, app->drawing_area);

    /* ── panel lateral ── */
    GtkBox *panel = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 10));

    /* ─ grupo métricas ─ */
    AdwPreferencesGroup *grp_metrics =
        ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_metrics, "Métricas en Tiempo Real");

    GtkBox *mbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 10));
    gtk_widget_set_margin_start(GTK_WIDGET(mbox), 12);
    gtk_widget_set_margin_end  (GTK_WIDGET(mbox), 12);
    gtk_widget_set_margin_top  (GTK_WIDGET(mbox), 8);
    gtk_widget_set_margin_bottom(GTK_WIDGET(mbox), 8);

    make_metric_row(mbox, "FPS",
                    "-- FPS",
                    &app->lbl_fps, &app->progress_fps, NULL);

    make_metric_row(mbox, "CPU (proceso)",
                    "CPU --%",
                    &app->lbl_cpu, &app->progress_cpu, NULL);

    /* separador visual */
    gtk_box_append(mbox, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkLabel *sep_lbl = GTK_LABEL(gtk_label_new("Desglose por frame"));
    gtk_widget_add_css_class(GTK_WIDGET(sep_lbl), "caption-heading");
    gtk_widget_set_halign(GTK_WIDGET(sep_lbl), GTK_ALIGN_START);
    gtk_box_append(mbox, GTK_WIDGET(sep_lbl));

    make_metric_row(mbox,
                    "⚡ Física  (hot path V4)",
                    "--",
                    &app->lbl_phys, &app->progress_phys, "accent");

    make_metric_row(mbox,
                    "🖌 Render  (Cairo/CPU)",
                    "--",
                    &app->lbl_render, &app->progress_render, NULL);

    GtkLabel *note = GTK_LABEL(gtk_label_new(
        "La barra de Física muestra su fracción\n"
        "del presupuesto de 1 frame (16.7 ms).\n"
        "Con V4 debería ser notablemente menor."));
    gtk_label_set_justify(GTK_LABEL(note), GTK_JUSTIFY_LEFT);
    gtk_widget_set_halign(GTK_WIDGET(note), GTK_ALIGN_START);
    gtk_widget_add_css_class(GTK_WIDGET(note), "caption");
    gtk_box_append(mbox, GTK_WIDGET(note));

    adw_preferences_group_add(grp_metrics, GTK_WIDGET(mbox));

    /* ─ grupo control ─ */
    AdwPreferencesGroup *grp_ctrl =
        ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_ctrl, "Control");

    GtkBox *cbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 10));
    gtk_widget_set_margin_start(GTK_WIDGET(cbox), 12);
    gtk_widget_set_margin_end  (GTK_WIDGET(cbox), 12);
    gtk_widget_set_margin_top  (GTK_WIDGET(cbox), 8);
    gtk_widget_set_margin_bottom(GTK_WIDGET(cbox), 8);

    app->lbl_sprites = gtk_label_new("500 sprites");
    gtk_widget_add_css_class(app->lbl_sprites, "heading");
    gtk_widget_set_halign(app->lbl_sprites, GTK_ALIGN_CENTER);

    app->scale_sprites = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 1, MAX_SPRITES, 1);
    gtk_range_set_value(GTK_RANGE(app->scale_sprites), DEFAULT_SPRITES);
    gtk_scale_set_draw_value(GTK_SCALE(app->scale_sprites), FALSE);
    gtk_widget_set_hexpand(app->scale_sprites, TRUE);
    g_signal_connect(app->scale_sprites, "value-changed",
                     G_CALLBACK(on_sprites_changed), app);
    gtk_scale_add_mark(GTK_SCALE(app->scale_sprites),    1, GTK_POS_BOTTOM, "1");
    gtk_scale_add_mark(GTK_SCALE(app->scale_sprites),  500, GTK_POS_BOTTOM, "500");
    gtk_scale_add_mark(GTK_SCALE(app->scale_sprites), 1000, GTK_POS_BOTTOM, "1k");
    gtk_scale_add_mark(GTK_SCALE(app->scale_sprites), 2500, GTK_POS_BOTTOM, "2.5k");
    gtk_scale_add_mark(GTK_SCALE(app->scale_sprites), 5000, GTK_POS_BOTTOM, "5k");

    app->btn_toggle = gtk_button_new_with_label("⏸ Pausar");
    gtk_widget_add_css_class(app->btn_toggle, "suggested-action");
    g_signal_connect(app->btn_toggle, "clicked", G_CALLBACK(on_toggle), app);

    GtkLabel *headless_hint = GTK_LABEL(gtk_label_new(
        "Para aislar la física sin Cairo:\n"
        "./sprite-bench --headless 5000 5"));
    gtk_label_set_justify(GTK_LABEL(headless_hint), GTK_JUSTIFY_LEFT);
    gtk_widget_set_halign(GTK_WIDGET(headless_hint), GTK_ALIGN_START);
    gtk_widget_add_css_class(GTK_WIDGET(headless_hint), "monospace");

    gtk_box_append(cbox, app->lbl_sprites);
    gtk_box_append(cbox, app->scale_sprites);
    gtk_box_append(cbox, app->btn_toggle);
    gtk_box_append(cbox, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(cbox, GTK_WIDGET(headless_hint));
    adw_preferences_group_add(grp_ctrl, GTK_WIDGET(cbox));

    /* ─ grupo build info ─ */
    AdwPreferencesGroup *grp_info =
        ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_info, "Build");

#ifdef __AVX512F__
    const char *build_str =
        "✅ AVX-512 (x86-64-v4)\n-O3 -march=x86-64-v4\n-ffast-math -funroll-loops";
#elif defined(__AVX2__)
    const char *build_str =
        "⚡ AVX2 (x86-64-v3)\n-O3 -march=x86-64-v3\n-ffast-math";
#else
    const char *build_str =
        "⚠️  Base (-O2)\nSin SIMD extendido\nUsa -Dvector_level=v4";
#endif
    app->lbl_build = gtk_label_new(build_str);
    gtk_label_set_justify(GTK_LABEL(app->lbl_build), GTK_JUSTIFY_LEFT);
    gtk_widget_set_halign(app->lbl_build, GTK_ALIGN_START);
    gtk_widget_add_css_class(app->lbl_build, "monospace");

    GtkBox *ibox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    gtk_widget_set_margin_start(GTK_WIDGET(ibox), 12);
    gtk_widget_set_margin_end  (GTK_WIDGET(ibox), 12);
    gtk_widget_set_margin_top  (GTK_WIDGET(ibox), 8);
    gtk_widget_set_margin_bottom(GTK_WIDGET(ibox), 8);
    gtk_box_append(ibox, app->lbl_build);
    adw_preferences_group_add(grp_info, GTK_WIDGET(ibox));

    /* ensamblar panel */
    gtk_widget_set_margin_top   (GTK_WIDGET(panel), 12);
    gtk_widget_set_margin_bottom(GTK_WIDGET(panel), 12);
    gtk_box_append(panel, GTK_WIDGET(grp_metrics));
    gtk_box_append(panel, GTK_WIDGET(grp_ctrl));
    gtk_box_append(panel, GTK_WIDGET(grp_info));

    GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
    gtk_scrolled_window_set_policy(scroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(scroll, GTK_WIDGET(panel));
    gtk_widget_set_size_request(GTK_WIDGET(scroll), 270, -1);

    gtk_box_append(hbox, gtk_separator_new(GTK_ORIENTATION_VERTICAL));
    gtk_box_append(hbox, GTK_WIDGET(scroll));

    adw_toolbar_view_set_content(toolbar_view, GTK_WIDGET(hbox));
    adw_application_window_set_content(win, GTK_WIDGET(toolbar_view));
    gtk_window_present(GTK_WINDOW(win));
}

/* ══════════════════════════════════════════════════════════════════
 *  ACTIVATE + MAIN
 * ══════════════════════════════════════════════════════════════════ */
static void on_activate(GtkApplication *gapp, gpointer user_data) {
    build_ui(gapp, user_data);
}

int main(int argc, char **argv) {
    /* ── modo headless ── */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) {
            int n = (i + 1 < argc) ? atoi(argv[i + 1]) : 2000;
            int s = (i + 2 < argc) ? atoi(argv[i + 2]) : 5;
            if (n <= 0 || n > MAX_SPRITES) n = 2000;
            if (s <= 0 || s > 3600)       s = 5;
            return run_headless(n, s);
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Uso: sprite-bench [--headless [N_SPRITES] [SEGUNDOS]]\n");
            printf("  --headless   Corre solo la física, sin ventana GTK\n");
            printf("  N_SPRITES    Número de sprites (default: 2000, max: %d)\n", MAX_SPRITES);
            printf("  SEGUNDOS     Duración del benchmark (default: 5)\n");
            return 0;
        }
    }

    /* ── modo GUI ── */
    srand((unsigned)time(NULL));

    AppData *app = g_new0(AppData, 1);
    app->n_sprites     = DEFAULT_SPRITES;
    app->running       = TRUE;
    app->canvas_w      = 860;
    app->canvas_h      = 640;
    app->last_frame_us = g_get_monotonic_time();
    app->last_cpu_time = read_proc_cpu_ticks();
    app->last_wall_us  = g_get_monotonic_time();

    init_sprites(app);

    app->cpu_timer = g_timeout_add(CPU_SAMPLE_MS, update_cpu, app);

    AdwApplication *adw_app =
        adw_application_new("dev.bench.SpriteBench",
                            G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(adw_app, "activate", G_CALLBACK(on_activate), app);
    int status = g_application_run(G_APPLICATION(adw_app), argc, argv);

    g_source_remove(app->cpu_timer);
    for (int i = 0; i < (int)(PALETTE_SIZE * 4); i++)
        cairo_surface_destroy(app->sprite_surfaces[i]);
    g_object_unref(adw_app);
    g_free(app);
    return status;
}

