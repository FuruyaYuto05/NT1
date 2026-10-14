#pragma pack_matrix(row_major)

cbuffer cbuff0 : register(b0) {
    matrix mat;   // 射影行列
    float4 color; // 色(RGBA)
};

struct VSInput {
    float4 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct VSOutput {
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.svpos = mul(input.pos, mat);
    output.uv = input.uv;
    return output;
}
