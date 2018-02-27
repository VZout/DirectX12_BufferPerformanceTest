cbuffer ConstantBuffer : register(b0)
{
	float4 pos;
	float4 color;
};

float4 main() : SV_TARGET
{
    return color;
}
