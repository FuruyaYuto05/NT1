#pragma pack_matrix(row_major)

cbuffer cbuff0 : register(b0) {
    float32_t4x4 mat; // ３Ｄ変換行列
    float32_t2 uvOffset; // UVオフセット
    float32_t2 uvScale; // UVスケール
    float32_t4 color; // 色 (RGBA)
    uint32_t textureDescriptorIndex; // デスクリプタインデックス
};

struct VSOutput {
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD;
};

SamplerState smp : register(s0);
Texture2D<float4> tex : register(t0);

float4 main(VSOutput input) : SV_TARGET {
    return tex.Sample(smp, input.uv) * color;
}
