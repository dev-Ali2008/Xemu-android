static GLchar texture_blit_frag_src[] =
    "#version 300 es\n"
    "\n"
    "uniform sampler2D image;\n"
    "in  mediump vec2 ex_tex_coord;\n"
    "out mediump vec4 out_frag_color;\n"
    "\n"
    "void main(void) {\n"
    "     out_frag_color = texture(image, ex_tex_coord);\n"
    "}\n"
    "\n";
