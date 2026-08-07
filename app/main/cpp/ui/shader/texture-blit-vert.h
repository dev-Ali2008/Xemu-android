static GLchar texture_blit_vert_src[] =
    "#version 300 es\n"
    "\n"
    "in vec2  in_position;\n"
    "out vec2 ex_tex_coord;\n"
    "\n"
    "void main(void) {\n"
    "    gl_Position = vec4(in_position, 0.0, 1.0);\n"
    "    ex_tex_coord = vec2(1.0 + in_position.x, 1.0 - in_position.y) * 0.5;\n"
    "}\n"
    "\n";
