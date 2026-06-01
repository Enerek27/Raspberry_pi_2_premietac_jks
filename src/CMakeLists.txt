add_executable(app_premietac main.c)

target_link_libraries(
    app_premietac 
    premietac
    uart
    raylib
    m          # matematika: sinf, cosf, fmaxf, sqrtf, ...
    atomic     # 32-bit ARM problém s -latomic[web:65]
    pthread
    dl
    rt
    X11)