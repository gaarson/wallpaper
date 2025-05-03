#version 100
precision highp float;
precision highp int;

attribute vec4 aPosition; // Vertex coords (-1..1)
attribute vec2 aTexCoord; // Base texture coords (0..1)

// Uniforms for aspect ratio correction ('cover' mode)
uniform vec2 uResolution;        // Physical screen resolution
uniform vec2 uTextureResolution; // Actual texture dimensions

varying vec2 vTexCoord; // Output: Corrected texture coordinates
// varying vec2 vDeviceCoord; // Not strictly needed by texture frag shader, but pass anyway if VS is shared

void main() {
    gl_Position = aPosition;
    // vDeviceCoord = aPosition.xy; // Pass if needed

    // Calculate texture coordinates for 'cover' scaling
    float screenAspect = 1.0;
    if (uResolution.y > 0.0) {
        screenAspect = uResolution.x / uResolution.y;
    }

    float textureAspect = 1.0;
    if (uTextureResolution.y > 0.0) {
        textureAspect = uTextureResolution.x / uTextureResolution.y;
    }

    float scaleX = 1.0, scaleY = 1.0;
    // Avoid division by zero or using aspect if resolution is invalid
    if (textureAspect > 0.0 && screenAspect > 0.0) {
         if (textureAspect > screenAspect) {
             // Texture wider than screen (aspect ratio > screen aspect ratio)
             // Fit texture height to screen height (scaleY = 1.0)
             // Scale texture width down to fit (scaleX < 1.0)
             scaleY = 1.0;
             scaleX = screenAspect / textureAspect;
         } else {
             // Texture taller than screen (or same aspect ratio)
             // Fit texture width to screen width (scaleX = 1.0)
             // Scale texture height down to fit (scaleY < 1.0)
             scaleX = 1.0;
             scaleY = textureAspect / screenAspect;
         }
    }

    // Center the texture coordinates and apply the calculated scale
    vTexCoord.x = (aTexCoord.x - 0.5) * scaleX + 0.5;
    vTexCoord.y = (aTexCoord.y - 0.5) * scaleY + 0.5;
}
