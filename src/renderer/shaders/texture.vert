#version 100
precision highp float;
precision highp int;

attribute vec4 aPosition; 
attribute vec2 aTexCoord; 


uniform vec2 uResolution;        
uniform vec2 uTextureResolution; 

varying vec2 vTexCoord; 


void main() {
    gl_Position = aPosition;
    

    
    float screenAspect = 1.0;
    if (uResolution.y > 0.0) {
        screenAspect = uResolution.x / uResolution.y;
    }

    float textureAspect = 1.0;
    if (uTextureResolution.y > 0.0) {
        textureAspect = uTextureResolution.x / uTextureResolution.y;
    }

    float scaleX = 1.0, scaleY = 1.0;
    
    if (textureAspect > 0.0 && screenAspect > 0.0) {
         if (textureAspect > screenAspect) {
             
             
             
             scaleY = 1.0;
             scaleX = screenAspect / textureAspect;
         } else {
             
             
             
             scaleX = 1.0;
             scaleY = textureAspect / screenAspect;
         }
    }

    
    vTexCoord.x = (aTexCoord.x - 0.5) * scaleX + 0.5;
    vTexCoord.y = (aTexCoord.y - 0.5) * scaleY + 0.5;
}
