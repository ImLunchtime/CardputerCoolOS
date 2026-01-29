file(GLOB_RECURSE MY_SRCS
    "./hal/*.c"
    "./hal/*.cc"
    "./hal/*.cpp"
    "./apps/*.c"
    "./apps/*.cc"
    "./apps/*.cpp"
)

list(APPEND MY_SRCS "./apps/app_music/music_player.cpp")
list(REMOVE_DUPLICATES MY_SRCS)

set(MY_INCLUDE_DIRS 
    "."
    "./assets"
    "./hal"
    "./apps"
)

file(GLOB EXTERN_FILES assets/*)

idf_component_register(SRCS "main.cpp" ${MY_SRCS}
                    INCLUDE_DIRS ${MY_INCLUDE_DIRS}
                    EMBED_FILES ${EXTERN_FILES}
)
