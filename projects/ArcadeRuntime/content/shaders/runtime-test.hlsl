struct VertexOutput
{
    float4 Position : SV_Position;
    float3 Color : COLOR0;
};

VertexOutput VertexMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(0.0, -0.5), float2(0.5, 0.5), float2(-0.5, 0.5)
    };
    const float3 colors[3] = {
        float3(1.0, 0.25, 0.15), float3(0.2, 0.75, 1.0), float3(0.4, 1.0, 0.35)
    };

    VertexOutput output;
    output.Position = float4(positions[vertexId], 0.0, 1.0);
    output.Color = colors[vertexId];
    return output;
}

float4 PixelMain(VertexOutput input) : SV_Target0
{
    return float4(input.Color, 1.0);
}
