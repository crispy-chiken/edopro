_find_package(${ARGS})

if (1)
    find_library(lua_cpp NAMES lua-c++ liblua-c++ REQUIRED)
    set(LUA_LIBRARIES ${LUA_LIBRARIES} ${lua_cpp})
endif()
