#include <stdbool.h>

#include "game/camera.h"
#include "game/level/boxes.h"
#include "game/level/level_editor/proto_rect.h"
#include "game/level/level_editor/color_picker.h"
#include "game/level/level_editor/layer.h"
#include "game/level/level_editor/point_layer.h"
#include "game/level/level_editor/layer_picker.h"
#include "system/stacktrace.h"
#include "system/nth_alloc.h"
#include "system/lt.h"

#include "level_editor.h"

struct LevelEditor
{
    Lt *lt;
    Vec camera_position;
    float camera_scale;
    LayerPicker layer_picker;
    ColorPicker color_picker;
    ProtoRect proto_rect;

    RectLayer *boxes_rect_layer;
    RectLayer *platforms_rect_layer;
    RectLayer *back_platforms_rect_layer;
    PointLayer *goals_rect_layer;

    RectLayer *current_rect_rect_layer;
    PointLayer *current_point_rect_layer;
    // TODO(#823): LevelEditor does not allow to switch the current layer
    bool drag;
};

LevelEditor *create_level_editor(RectLayer *boxes_rect_layer,
                                 RectLayer *platforms_rect_layer,
                                 RectLayer *back_platforms_rect_layer,
                                 PointLayer *goals_rect_layer)
{
    trace_assert(boxes_rect_layer);
    trace_assert(platforms_rect_layer);
    trace_assert(back_platforms_rect_layer);
    trace_assert(goals_rect_layer);

    Lt *lt = create_lt();
    if (lt == NULL) {
        return NULL;
    }

    LevelEditor *level_editor = PUSH_LT(lt, nth_calloc(1, sizeof(LevelEditor)), free);
    if (level_editor == NULL) {
        RETURN_LT(lt, NULL);
    }
    level_editor->lt = lt;

    level_editor->camera_position = vec(0.0f, 0.0f);
    level_editor->camera_scale = 1.0f;

    level_editor->boxes_rect_layer = PUSH_LT(lt, boxes_rect_layer, destroy_rect_layer);
    level_editor->platforms_rect_layer = PUSH_LT(lt, platforms_rect_layer, destroy_rect_layer);
    level_editor->back_platforms_rect_layer = PUSH_LT(lt, back_platforms_rect_layer, destroy_rect_layer);
    level_editor->goals_rect_layer = PUSH_LT(lt, goals_rect_layer, destroy_point_rect_layer);
    level_editor->current_rect_rect_layer = boxes_rect_layer;
    level_editor->current_point_rect_layer = goals_rect_layer;

    level_editor->color_picker.color = rgba(1.0f, 0.0f, 0.0f, 1.0f);
    level_editor->layer_picker = LAYER_PICKER_BOXES;

    level_editor->proto_rect.color_current = &level_editor->color_picker.color;
    level_editor->proto_rect.layer_current = &level_editor->current_rect_rect_layer;

    level_editor->drag = false;

    return level_editor;
}

void destroy_level_editor(LevelEditor *level_editor)
{
    trace_assert(level_editor);
    RETURN_LT0(level_editor->lt);
}

int level_editor_render(const LevelEditor *level_editor,
                        Camera *camera)
{
    trace_assert(level_editor);
    trace_assert(camera);

    if (layer_render(level_editor->back_platforms_rect_layer, camera) < 0) {
        return -1;
    }

    if (layer_render(level_editor->boxes_rect_layer, camera) < 0) {
        return -1;
    }

    if (layer_render(level_editor->platforms_rect_layer, camera) < 0) {
        return -1;
    }

    if (proto_rect_render(&level_editor->proto_rect, camera) < 0) {
        return -1;
    }

    if (color_picker_render(&level_editor->color_picker, camera) < 0) {
        return -1;
    }

    if (layer_picker_render(&level_editor->layer_picker, camera) < 0) {
        return -1;
    }

    return 0;
}

int level_editor_update(LevelEditor *level_editor,
                        float delta_time)
{
    trace_assert(level_editor);

    if (proto_rect_update(&level_editor->proto_rect, delta_time) < 0) {
        return -1;
    }

    return 0;
}

int level_editor_event(LevelEditor *level_editor,
                       const SDL_Event *event,
                       const Camera *camera)
{
    trace_assert(level_editor);
    trace_assert(event);
    trace_assert(camera);

    switch (event->type) {
    case SDL_MOUSEWHEEL: {
        // TODO(#679): zooming in edit mode is not smooth enough
        if (event->wheel.y > 0) {
            level_editor->camera_scale += 0.1f;
        } else if (event->wheel.y < 0) {
            level_editor->camera_scale = fmaxf(0.1f, level_editor->camera_scale - 0.1f);
        }
    } break;

    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEBUTTONDOWN: {
        bool selected = false;
        if (color_picker_mouse_button(
                &level_editor->color_picker,
                &event->button,
                &selected) < 0) {
            return -1;
        }

        if (layer_picker_mouse_button(
                &level_editor->layer_picker,
                camera,
                &event->button,
                &selected) < 0) {
            return -1;
        }

        switch (level_editor->layer_picker) {
        case LAYER_PICKER_BOXES: {
            level_editor->current_rect_rect_layer = level_editor->boxes_rect_layer;
        } break;
        case LAYER_PICKER_PLATFORMS: {
            level_editor->current_rect_rect_layer = level_editor->platforms_rect_layer;
        } break;
        case LAYER_PICKER_BACK_PLATFORMS: {
            level_editor->current_rect_rect_layer = level_editor->back_platforms_rect_layer;
        } break;
        case LAYER_PICKER_GOALS: {
            level_editor->current_point_rect_layer = level_editor->goals_rect_layer;
        } break;

        default: {}
        }

        if (layer_picker_type(&level_editor->layer_picker) == LAYER_TYPE_RECT) {
            if (!selected && proto_rect_mouse_button(
                    &level_editor->proto_rect,
                    &event->button,
                    camera) < 0) {
                return -1;
            }
        } else {
            point_layer_mouse_button(level_editor->current_point_rect_layer, &event->button);
        }

        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_MIDDLE) {
            level_editor->drag = true;
        }

        if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_MIDDLE) {
            level_editor->drag = false;
        }
    } break;

    case SDL_MOUSEMOTION: {
        if (level_editor->drag) {
            const Vec next_position = camera_map_screen(camera, event->motion.x, event->motion.y);
            const Vec prev_position = camera_map_screen(
                camera,
                event->motion.x + event->motion.xrel,
                event->motion.y + event->motion.yrel);

            vec_add(&level_editor->camera_position,
                    vec_sub(next_position, prev_position));
        }

        if (proto_rect_mouse_motion(&level_editor->proto_rect, &event->motion, camera) < 0) {
            return -1;
        }
    } break;
    }

    return 0;
}

int level_editor_focus_camera(LevelEditor *level_editor,
                              Camera *camera)
{
    camera_center_at(camera, level_editor->camera_position);
    camera_scale(camera, level_editor->camera_scale);
    return 0;
}

const RectLayer *level_editor_boxes(const LevelEditor *level_editor)
{
    return level_editor->boxes_rect_layer;
}

const RectLayer *level_editor_platforms(const LevelEditor *level_editor)
{
    return level_editor->platforms_rect_layer;
}

const RectLayer *level_editor_back_platforms(const LevelEditor *level_editor)
{
    return level_editor->back_platforms_rect_layer;
}
