# FunPay Vertex tools.

add_executable(fpv_db_migrate fpv_db_migrate.c)
target_include_directories(fpv_db_migrate PRIVATE ${CMAKE_SOURCE_DIR}/fpv_core/src)
target_link_libraries(fpv_db_migrate PRIVATE fpv_core)

add_executable(fpv_db_backup fpv_db_backup.c)
target_include_directories(fpv_db_backup PRIVATE ${CMAKE_SOURCE_DIR}/fpv_core/src)
target_link_libraries(fpv_db_backup PRIVATE fpv_core)

add_executable(fpv_db_restore fpv_db_restore.c)
target_include_directories(fpv_db_restore PRIVATE ${CMAKE_SOURCE_DIR}/fpv_core/src)
target_link_libraries(fpv_db_restore PRIVATE fpv_core)

add_executable(fpv_db_seed fpv_db_seed.c)
target_include_directories(fpv_db_seed PRIVATE ${CMAKE_SOURCE_DIR}/fpv_core/src)
target_link_libraries(fpv_db_seed PRIVATE fpv_core)
