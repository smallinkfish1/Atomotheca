#version 330 core

out vec4 FragColor;

uniform vec3 objectColor;   // 让 CPU 能控制物体颜色（今天给个青色）

void main()
{
    FragColor = vec4(objectColor, 1.0);
}
