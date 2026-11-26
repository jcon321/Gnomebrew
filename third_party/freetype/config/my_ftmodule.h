/* my-client: the modules FreeType is built with (FT_CONFIG_MODULES_H). Only TrueType fonts
   (Arial), drawn antialiased by the smooth renderer. */
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
/* EOF */
