#define E_COMP_WL
#include "e.h"
#include <Ecore_Drm.h>

EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "Wl_Drm" };

static Ecore_Event_Handler *activate_handler;
static Ecore_Event_Handler *output_handler;
static Eina_Bool session_state = EINA_FALSE;

static Eina_Bool
_e_mod_drm_cb_activate(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Activate *e;

   if (!(e = event)) goto end;

   if (e->active)
     {
        E_Client *ec;

        if (session_state) goto end;
        session_state = EINA_TRUE;

        ecore_evas_show(e_comp->ee);
        E_CLIENT_FOREACH(ec)
          {
             if (ec->visible && (!ec->input_only))
               e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
          }
        e_comp_render_queue();
        e_comp_shape_queue_block(0);
        ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
     }
   else
     {
        session_state = EINA_FALSE;
        ecore_evas_hide(e_comp->ee);
        edje_file_cache_flush();
        edje_collection_cache_flush();
        evas_image_cache_flush(e_comp->evas);
        evas_font_cache_flush(e_comp->evas);
        evas_render_dump(e_comp->evas);

        e_comp_render_queue();
        e_comp_shape_queue_block(1);
        ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_mod_drm_cb_output(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Output *e;
   char buff[PATH_MAX];

   if (!(e = event)) goto end;

   if (!e->plug)
     {
        DBG("Caught Drm Output Unplug Event");
        /* FIXME: This needs to remove output from e_comp_wl */
        goto end;
     }

   snprintf(buff, sizeof(buff), "%d", e->id);

   if (!e_comp_wl_output_init(buff, e->make, e->model, e->x, e->y, e->w, e->h, 
                              e->phys_width, e->phys_height, e->refresh, 
                              e->subpixel_order, e->transform))
     {
        ERR("Could not setup new output: %s", buff);
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_mod_drm_cb_ee_resize(Ecore_Evas *ee EINA_UNUSED)
{
   e_comp_canvas_update();
}

EAPI void *
e_modapi_init(E_Module *m)
{
   int w = 0, h = 0;

   printf("LOAD WL_DRM MODULE\n");

   /* try to init ecore_drm */
   /* if (!ecore_drm_init()) */
   /*   { */
   /*      fprintf(stderr, "Could not initialize ecore_drm"); */
   /*      return NULL; */
   /*   } */

   if (!e_comp)
     {
        e_comp_new();
        e_comp->comp_type = E_PIXMAP_TYPE_WL;
     }

   if (e_comp_config_get()->engine == E_COMP_ENGINE_GL)
     {
        e_comp->ee = ecore_evas_gl_drm_new(NULL, 0, 0, 0, 1, 1);
        e_comp_gl_set(!!e_comp->ee);
     }

   /* fallback to framebuffer drm (non-accel) */
   if (!e_comp->ee)
     {
        if ((e_comp->ee = ecore_evas_drm_new(NULL, 0, 0, 0, 1, 1)))
          {
             e_comp_gl_set(EINA_FALSE);
             elm_config_accel_preference_set("none");
             elm_config_accel_preference_override_set(EINA_TRUE);
             elm_config_all_flush();
             elm_config_save();
          }
        else
          {
             fprintf(stderr, "Could not create ecore_evas_drm canvas");
             return NULL;
          }
     }

   ecore_evas_data_set(e_comp->ee, "comp", e_comp);

   /* get the current screen geometry */
   ecore_evas_screen_geometry_get(e_comp->ee, NULL, NULL, &w, &h);

   /* resize the canvas */
   ecore_evas_resize(e_comp->ee, w, h);

   ecore_evas_callback_resize_set(e_comp->ee, _e_mod_drm_cb_ee_resize);

   if (!e_xinerama_fake_screens_exist())
     {
        E_Screen *screen;

        screen = E_NEW(E_Screen, 1);
        screen->escreen = screen->screen = 0;
        screen->x = 0;
        screen->y = 0;
        screen->w = w;
        screen->h = h;
        e_xinerama_screens_set(eina_list_append(NULL, screen));
     }

   if (!e_comp_canvas_init(w, h)) return NULL;

   /* NB: This needs to be called AFTER the comp canvas has been setup */
   if (!e_comp_wl_init()) return NULL;

   e_comp_wl_input_pointer_enabled_set(e_comp->wl_comp_data, EINA_TRUE);
   e_comp_wl_input_keyboard_enabled_set(e_comp->wl_comp_data, EINA_TRUE);

   /* comp->pointer =  */
   /*   e_pointer_window_new(ecore_evas_window_get(comp->ee), 1); */
   e_comp->pointer = e_pointer_canvas_new(e_comp->ee, EINA_TRUE);
   e_comp->pointer->color = EINA_TRUE;

   /* FIXME: We need a way to trap for user changing the keymap inside of E
    *        without the event coming from X11 */

   /* FIXME: We should make a decision here ...
    *
    * Fetch the keymap from drm, OR set this to what the E config is....
    */

   /* FIXME: This is just for testing at the moment....
    * happens to jive with what drm does */
   e_comp_wl_input_keymap_set(e_comp->wl_comp_data, NULL, NULL, NULL);

   activate_handler =
      ecore_event_handler_add(ECORE_DRM_EVENT_ACTIVATE,
                              _e_mod_drm_cb_activate, NULL);

   output_handler =
      ecore_event_handler_add(ECORE_DRM_EVENT_OUTPUT,
                              _e_mod_drm_cb_output, NULL);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   /* shutdown ecore_drm */
   /* ecore_drm_shutdown(); */

   if (output_handler) ecore_event_handler_del(output_handler);
   output_handler = NULL;

   if (activate_handler) ecore_event_handler_del(activate_handler);
   activate_handler = NULL;

   return 1;
}
