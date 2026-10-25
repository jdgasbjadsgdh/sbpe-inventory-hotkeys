/* Rendering-only native Mac SBPE companion. Call after visual_prepare and
 * restore before visual_restore, around the original Stage::draw call. */
struct MinimalBackgroundState {
    struct Stage *stage;
    struct WorldView *view;
    uint32_t color;
    void *fill_finish;
};

static void minimal_background_prepare(struct Stage *stage, struct WorldView *view,
                                       struct MinimalBackgroundState *saved) {
    int legacy_white = getbool("plugin_minimal_textures", "white_background", 0);
    if (!stage || !view || !getbool("plugin_minimal_textures", "flat_background", legacy_white))
        return;
    saved->stage = stage;
    saved->view = view;
    saved->color = stage->backgroundColor;
    saved->fill_finish = view->fills.finish;
    stage->backgroundColor = 0xff000000 | (color("plugin_minimal_textures", "background_color",
                                              legacy_white ? "ffffff" : "000000") & 0xffffff);
    view->fills.finish = view->fills.start;
}

static void minimal_background_restore(struct MinimalBackgroundState *saved) {
    if (!saved->stage)
        return;
    saved->stage->backgroundColor = saved->color;
    saved->view->fills.finish = saved->fill_finish;
    saved->stage = NULL;
    saved->view = NULL;
}
