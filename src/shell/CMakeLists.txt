target_sources_ifdef(CONFIG_ZMK_RRL_SHELL app PRIVATE rrl_shell.c)
set_source_files_properties(rrl_shell.c PROPERTIES COMPILE_FLAGS "-Os")
