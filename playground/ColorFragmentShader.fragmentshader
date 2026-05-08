#version 330 core

in vec2 UV;
in vec3 FragPos;
in vec3 Normal;

// Ouput data
layout(location = 0) out vec4 color;

uniform int colorCheck;
uniform sampler2D myTextureSampler;

// Lighting uniforms
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;

void main(){
    vec3 objectColor;

    if (colorCheck == -1) //use texture
    {
        objectColor = texture(myTextureSampler, UV).rgb;
    }
	else if (colorCheck == 1) //R
	{
		objectColor = vec3(1, 0, 0);
	}
	else if (colorCheck == 2) //G
	{
		objectColor = vec3(0, 1, 0);
	}
    else if (colorCheck == 3) //B
    {
        objectColor = vec3(0, 0, 1);
    }
    else if (colorCheck == 4) //Brown
    {
        objectColor = vec3(0.714, 0.494, 0.212);
    }
    else // wrong value. default texture will be used
    {
        objectColor = texture(myTextureSampler, UV).rgb;
    }
    
    // Ambient
    float ambientStrength = 0.5;
    vec3 ambient = ambientStrength * objectColor;

    // Diffuse
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = 0.4 * diff * objectColor;

    // Specular
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = 0.5 * spec * lightColor;

    vec3 result = ambient + diffuse + specular;
    color = vec4(result, 1.0);
}

