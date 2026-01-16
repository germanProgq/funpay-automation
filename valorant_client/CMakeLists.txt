# Valorant client app.

add_executable(valorant_client
  src/main.c
)

target_compile_features(valorant_client PRIVATE c_std_11)

find_package(CURL REQUIRED)

target_link_libraries(valorant_client PRIVATE CURL::libcurl)
