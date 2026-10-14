#include "Shape.hlsli"

float3 ApplySRGBGamma(float3 linearColor)
{
    return select(linearColor < 0.0031308f, 12.92f * linearColor, 1.055f * pow(abs(linearColor), 1.0f / 2.4f) - 0.055f);
}

float4 main(VSOutput input) : SV_TARGET {
    float4 output = input.color;
    output.xyz = ApplySRGBGamma(output.xyz);
    return output;
}