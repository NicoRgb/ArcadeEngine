struct VertexOutput
{
    float4 Position : SV_Position;
    float3 Color : COLOR0;
};

[shader("vertex")]
VertexOutput VertexMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(0.0, 0.65),
        float2(0.65, -0.55),
        float2(-0.65, -0.55),
    };
    VertexOutput output;
    output.Position = float4(positions[vertexId], 0.0, 1.0);
    output.Color = float3(0.36, 0.72, 0.95);
    return output;
}

[shader("fragment")]
float4 PixelMain(VertexOutput input) : SV_Target0
{
    return float4(input.Color, 1.0);
}
