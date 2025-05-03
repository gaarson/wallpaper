#version 100
precision highp float;
precision highp int;

attribute vec4 aPosition; // Vertex coords (-1..1)
attribute vec2 aTexCoord; // Texture coords (0..1) - passed but not used by gradient fragment shader

// Uniforms for texture aspect ratio calculation (kept for compatibility if VS is shared)
uniform vec2 uResolution;        // Physical screen resolution
uniform vec2 uTextureResolution; // Usually 1x1 if no texture

varying vec2 vDeviceCoord; // Pass device coords (-1..1) to fragment shader
varying vec2 vTexCoord;    // Pass texture coords (calculated for 'cover')

void main() {
    gl_Position = aPosition; // Output position
    vDeviceCoord = aPosition.xy; // Pass device coords directly

    // Calculate texture coordinates for 'cover' mode (needed if VS is shared)
    // If this VS is ONLY for gradient, this calculation can be removed.
    float screenAspect = 1.0; if (uResolution.y > 0.0) screenAspect = uResolution.x / uResolution.y;
    float textureAspect = 1.0; if (uTextureResolution.y > 0.0) textureAspect = uTextureResolution.x / uTextureResolution.y;
    float scaleX = 1.0, scaleY = 1.0;
    // Avoid division by zero
    if (textureAspect > 0.0 && screenAspect > 0.0) {
        if (textureAspect > screenAspect) {
            scaleX = screenAspect / textureAspect; // Texture wider, fit height, scale X down
        } else {
            scaleY = textureAspect / screenAspect; // Texture taller, fit width, scale Y down
        }
    }
    // Apply scale centered
    vTexCoord.x = (aTexCoord.x - 0.5) * scaleX + 0.5;
    vTexCoord.y = (aTexCoord.y - 0.5) * scaleY + 0.5;
}
