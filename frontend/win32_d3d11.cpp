#include "win32_d3d11.h"

#include "win32_text.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
	// The scene shaders do what the software rasterizer does (render/rasterizer.cpp): the
	// vertex light (color * scale + add * the vertex's alpha, per batch), texture times the
	// lit color, linear fog on view depth, masked texels (alpha under half) dropped, and a
	// translucent material's opacity.
	// Depth is reversed (1 at the near plane, 0 at the far plane and where nothing is
	// drawn) for precision across a zone. The fullscreen triangle clears a view's block
	// and composites the canvas: its top byte is how much of the view shows through.
	const char ShaderSource[] = R"(
cbuffer Constants : register(b0)
{
	float4 eye;         // xyz
	float4 right;       // xyz: the camera's right, scaled to clip space
	float4 up;          // xyz: up, likewise
	float4 forward;     // xyz
	float4 depth;       // clip z = x * view z + y
	float4 fog_color;   // rgb
	float4 fog;         // x: start, y: 1 / (end - start), 0 for none
	float4 clear_color; // rgb
	float4 sun_direction; // xyz: the way the sun shines
	float4 sun_color;     // rgb
	float4 light_position[8]; // xyz: the lights spawns carry
	float4 light_strength[8]; // rgb: color x 0.05 radius^2
};

cbuffer Batch : register(b1)
{
	float4 light_scale; // rgb, and the material's opacity in a
	float4 light_add;   // rgb
	float4 dynamic_lights; // x: the sun on, y: the carried lights' mask, z: not fogged, w: texel alpha (1 translucent, 2 additive)
	float4 light_base;     // rgb: added to every vertex
};

Texture2D image : register(t0);
SamplerState linear_wrap : register(s0);

struct VertexIn
{
	float3 position : POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
	float3 normal : NORMAL;
};

struct PixelIn
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
	float depth : TEXCOORD1;
	float texel_alpha : TEXCOORD2; // Material::texel_alpha: 1 translucent, 2 additive
};

PixelIn SceneVS(VertexIn input)
{
	float3 d = input.position - eye.xyz;
	float z = dot(d, forward.xyz);
	PixelIn output;
	output.position = float4(dot(d, right.xyz), dot(d, up.xyz), depth.x * z + depth.y, z);
	output.uv = input.uv;
	float3 shade = input.color.rgb * light_scale.rgb + light_add.rgb * input.color.a + light_base.rgb;
	if (dynamic_lights.x > 0)
		shade += sun_color.rgb * max(0, -dot(input.normal, sun_direction.xyz)) * input.color.a;
	uint mask = (uint)dynamic_lights.y;
	for (uint i = 0; i < 8; ++i) {
		if (mask & (1u << i)) {
			float3 to = light_position[i].xyz - input.position;
			float facing = dot(to, input.normal);
			if (facing > 0) {
				float distance2 = dot(to, to);
				shade += light_strength[i].rgb * (facing / (distance2 * sqrt(distance2)));
			}
		}
	}
	output.color = float4(min(shade, 1), light_scale.a);
	output.depth = dynamic_lights.z > 0 ? -1e6 : z; // clear of the fog's start
	output.texel_alpha = dynamic_lights.w;
	return output;
}

float4 Shade(PixelIn input, float4 texel)
{
	float3 color = texel.rgb * input.color.rgb;
	color = lerp(color, fog_color.rgb, saturate((input.depth - fog.x) * fog.y));
	return float4(color, input.color.a);
}

float4 ScenePS(PixelIn input) : SV_Target
{
	float4 texel = image.Sample(linear_wrap, input.uv);
	float4 color = Shade(input, texel);
	if (input.texel_alpha > 0) {
		// The texel's alpha blends too, and under 8 it's dropped (eqgfx's particle system).
		color.a *= texel.a;
		clip(color.a - 8.0 / 255);
		if (input.texel_alpha > 1.5)
			color.rgb *= color.a; // additive: ONE, ONE
	}
	return color;
}

float4 MaskedPS(PixelIn input) : SV_Target
{
	float4 texel = image.Sample(linear_wrap, input.uv);
	clip(texel.a - 0.5);
	return Shade(input, texel);
}

float4 FullscreenVS(uint id : SV_VertexID) : SV_Position
{
	float2 p = float2((id << 1) & 2, id & 2);
	return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 ClearPS() : SV_Target
{
	return float4(clear_color.rgb, 1);
}

float4 CompositePS(float4 position : SV_Position) : SV_Target
{
	return image.Load(int3(position.xy, 0));
}
)";

	using CompileFunction = HRESULT(WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR,
		LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

	struct PassConstants
	{
		float eye[4], right[4], up[4], forward[4], depth[4], fog_color[4], fog[4], clear_color[4];
		float sun_direction[4], sun_color[4];
		float light_position[8][4], light_strength[8][4];
	};

	void SetColor(float *out, uint32_t rgb)
	{
		out[0] = ((rgb >> 16) & 0xff) / 255.0f;
		out[1] = ((rgb >> 8) & 0xff) / 255.0f;
		out[2] = (rgb & 0xff) / 255.0f;
		out[3] = 1;
	}

	void SetVector(float *out, const render::Vec3 &v)
	{
		out[0] = v.x;
		out[1] = v.y;
		out[2] = v.z;
		out[3] = 0;
	}

	// A Texture's GPU copy (render::Texture::device_texture).
	struct GpuTexture
	{
		ID3D11Device *device = nullptr;
		ComPtr<ID3D11ShaderResourceView> view;
	};

	// A vertex on the GPU: render::Vertex with its color as it is (the shader lights it).
	struct GpuVertex
	{
		float x, y, z, u, v;
		uint32_t color; // R8G8B8A8
		float nx, ny, nz;

		static GpuVertex From(const render::Vertex &in)
		{
			uint32_t c = in.color; // 0xAARRGGBB
			return {in.position.x, in.position.y, in.position.z, in.u, in.v,
				((c >> 16) & 0xff) | (c & 0xff00) | ((c & 0xff) << 16) | (c & 0xff000000), in.normal.x, in.normal.y,
				in.normal.z};
		}
	};

	// StaticVertices' GPU copy (render::StaticVertices::device_buffer).
	struct GpuBuffer
	{
		ID3D11Device *device = nullptr;
		ComPtr<ID3D11Buffer> buffer;
	};

	std::string HresultText(HRESULT hr)
	{
		char text[16];
		snprintf(text, sizeof(text), "0x%08lx", static_cast<unsigned long>(hr));
		return text;
	}
}

struct D3d11Device::State
{
	HWND window = nullptr;
	int width = 0, height = 0;
	bool vsync = true;
	bool tearing = false; // presents without vsync may tear (flip model otherwise waits for the compositor)
	bool lost = false;
	double upload_ms = 0; // the last Present's copy of the canvas into its texture
	std::string adapter;

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<IDXGISwapChain> swap_chain;
	ComPtr<ID3D11RenderTargetView> back_buffer;
	ComPtr<ID3D11Texture2D> depth;
	ComPtr<ID3D11DepthStencilView> depth_view;

	ComPtr<ID3D11VertexShader> scene_vs, fullscreen_vs;
	ComPtr<ID3D11PixelShader> scene_ps, masked_ps, clear_ps, composite_ps;
	ComPtr<ID3D11InputLayout> layout;
	ComPtr<ID3D11Buffer> scene_pass, background_pass, batch_constants;
	ComPtr<ID3D11SamplerState> sampler;
	ComPtr<ID3D11BlendState> blend_opaque, blend_alpha, blend_add, blend_no_color, blend_composite;
	ComPtr<ID3D11DepthStencilState> depth_write, depth_test, depth_background, depth_always;
	ComPtr<ID3D11RasterizerState> cull_back, cull_none;
	ComPtr<ID3D11ShaderResourceView> white;

	// The front end's canvas, uploaded each frame: the rows drawn on this frame or the last
	// (canvas_dirty), the rest being as they were.
	uint32_t *canvas = nullptr;
	int canvas_width = 0, canvas_height = 0, canvas_stride = 0;
	ComPtr<ID3D11Texture2D> canvas_texture;
	ComPtr<ID3D11ShaderResourceView> canvas_view;
	CanvasDirty canvas_dirty;

	// A frame's triangles, written whole at Finish.
	ComPtr<ID3D11Buffer> vertex_buffer, index_buffer;
	size_t vertex_capacity = 0, index_capacity = 0;

	ID3D11ShaderResourceView *TextureView(const render::Texture *texture);
	ID3D11Buffer *StaticBuffer(const render::StaticVertices &pool);
	bool Upload(ComPtr<ID3D11Buffer> &buffer, size_t &capacity, UINT bind, const void *data, size_t bytes);
};

ID3D11ShaderResourceView *D3d11Device::State::TextureView(const render::Texture *texture)
{
	if (!texture || texture->levels.empty())
		return white.Get();
	auto *gpu = static_cast<GpuTexture *>(texture->device_texture.get());
	if (gpu && gpu->device == device.Get())
		return gpu->view.Get();

	std::vector<D3D11_SUBRESOURCE_DATA> data(texture->levels.size());
	for (size_t i = 0; i < data.size(); ++i) {
		int w = std::max(1, texture->width >> i), h = std::max(1, texture->height >> i);
		if (texture->levels[i].size() != static_cast<size_t>(w) * h)
			return white.Get();
		data[i].pSysMem = texture->levels[i].data();
		data[i].SysMemPitch = static_cast<UINT>(w) * 4;
	}
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = static_cast<UINT>(texture->width);
	desc.Height = static_cast<UINT>(texture->height);
	desc.MipLevels = static_cast<UINT>(data.size());
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // 0xAARRGGBB in memory order
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	ComPtr<ID3D11Texture2D> resource;
	auto made = std::make_shared<GpuTexture>();
	if (FAILED(device->CreateTexture2D(&desc, data.data(), &resource)) ||
	    FAILED(device->CreateShaderResourceView(resource.Get(), nullptr, &made->view)))
		return white.Get();
	made->device = device.Get();
	texture->device_texture = made;
	return made->view.Get();
}

ID3D11Buffer *D3d11Device::State::StaticBuffer(const render::StaticVertices &pool)
{
	auto *gpu = static_cast<GpuBuffer *>(pool.device_buffer.get());
	if (gpu && gpu->device == device.Get())
		return gpu->buffer.Get();
	if (pool.vertices.empty())
		return nullptr;
	std::vector<GpuVertex> vertices(pool.vertices.size());
	for (size_t i = 0; i < vertices.size(); ++i)
		vertices[i] = GpuVertex::From(pool.vertices[i]);
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(GpuVertex));
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA data{vertices.data(), 0, 0};
	auto made = std::make_shared<GpuBuffer>();
	if (FAILED(device->CreateBuffer(&desc, &data, &made->buffer)))
		return nullptr;
	made->device = device.Get();
	pool.device_buffer = made;
	return made->buffer.Get();
}

bool D3d11Device::State::Upload(ComPtr<ID3D11Buffer> &buffer, size_t &capacity, UINT bind, const void *data, size_t bytes)
{
	if (bytes == 0)
		return true;
	if (!buffer || bytes > capacity) {
		capacity = std::max({bytes, capacity * 2, static_cast<size_t>(1) << 20});
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = static_cast<UINT>(capacity);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = bind;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		buffer.Reset();
		if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer))) {
			capacity = 0;
			return false;
		}
	}
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;
	memcpy(mapped.pData, data, bytes);
	context->Unmap(buffer.Get(), 0);
	return true;
}

namespace
{
	class D3d11Renderer : public render::Renderer
	{
	public:
		explicit D3d11Renderer(D3d11Device::State &state) : m(state) {}
		~D3d11Renderer() override { Unmap(); }

		const char *Name() const override { return "Direct3D 11"; }
		void Begin(int width, int height, const render::Camera &camera) override;
		void Draw(const render::Vertex *vertices, size_t vertex_count, const uint32_t *indices, size_t index_count,
			const render::Material &material, bool reuse_vertices = false) override;
		void DrawStatic(const render::StaticVertices &pool, size_t first_vertex, size_t vertex_count, const uint32_t *indices,
			size_t index_count, const render::Material &material, bool reuse_vertices = false) override;
		void Finish(uint32_t *pixels, int stride, uint32_t clear_color, bool clear = true) override;
		bool Occluded(const render::Vec3 &point) const override;
		size_t TriangleCount() const override { return m_triangle_count; }
		void DrawEdges(uint32_t *pixels, int stride, uint32_t color) const override;

	private:
		// The Batch constants: the vertex light's scale and the opacity, its add, the dynamic
		// lights (the sun on, the carried lights' mask) and whether it's fogged, and its base.
		using Light = std::array<float, 16>;
		// One Draw: a run of the frame's indices.
		struct Batch
		{
			uint32_t first, count;
			ID3D11ShaderResourceView *texture;
			render::Blend blend;
			bool two_sided, background;
			Light light;
			// Static vertices: their GPU buffer, which the indices count in. Otherwise the frame's.
			const render::StaticVertices *pool;
			ID3D11Buffer *buffer;

			bool Solid() const { return blend == render::Blend::Opaque || blend == render::Blend::Masked; }
			bool SameState(const Batch &other) const
			{
				return texture == other.texture && blend == other.blend && two_sided == other.two_sided &&
				       background == other.background && light == other.light && buffer == other.buffer;
			}
			bool StateBefore(const Batch &other) const
			{
				return std::tie(buffer, texture, blend, two_sided, light) <
				       std::tie(other.buffer, other.texture, other.blend, other.two_sided, other.light);
			}
		};
		struct SortedTriangle
		{
			float depth; // farthest vertex
			uint32_t batch, first;
		};
		// The depth buffer copied back for Occluded, a few frames late, with the camera it
		// was drawn with.
		struct DepthCopy
		{
			ComPtr<ID3D11Texture2D> texture;
			uint64_t frame = 0; // 0: never written
			int view_x = 0, view_y = 0, width = 0, height = 0;
			render::Vec3 eye, right, up, forward;
			float focal = 1, near_plane = 1, depth_a = 0, depth_b = 1;
		};

		PassConstants Pass(bool background, uint32_t clear_color) const;
		void AddBatch(const uint32_t *indices, size_t index_count, size_t vertex_count, uint32_t base,
			const render::Material &material, const render::StaticVertices *pool, ID3D11Buffer *buffer);
		render::Vec3 Position(const Batch &batch, uint32_t index) const
		{
			if (batch.pool)
				return batch.pool->vertices[index].position;
			const GpuVertex &v = m_vertices[index];
			return {v.x, v.y, v.z};
		}
		void DrawRun(const Batch &state, uint32_t first, uint32_t count);
		void CopyDepth(int view_x, int view_y, float depth_a, float depth_b);
		const DepthCopy *ReadyDepth() const;
		void Unmap() const;

		D3d11Device::State &m;
		std::vector<GpuVertex> m_vertices;
		std::vector<uint32_t> m_indices;
		std::vector<Batch> m_batches;
		std::vector<Batch> m_grouped; // the solid batches by material, their indices copied together
		std::vector<SortedTriangle> m_sorted;
		size_t m_triangle_count = 0;
		uint64_t m_frame = 0;
		// The vertex array the last Draw copied, and where, for batches that reuse it.
		const render::Vertex *m_copied = nullptr;
		size_t m_copied_count = 0;
		uint32_t m_copied_base = 0;

		// Occluded asks for the depth buffer; Finish copies it while it's being asked for. A
		// copy is only done once the GPU has drawn its frame, which with vsync and frames
		// queued ahead can be several frames on, so there are a few in flight. The newest
		// finished one stays mapped (and isn't copied over) until a newer one is done.
		static constexpr int DepthCopies = 5;
		mutable uint64_t m_depth_wanted = 0;
		mutable uint64_t m_depth_checked = 0; // the frame ReadyDepth last looked for a newer copy
		mutable DepthCopy m_depth[DepthCopies];
		mutable int m_mapped = -1;
		mutable D3D11_MAPPED_SUBRESOURCE m_mapping{};

		// What the last DrawRun set, to skip repeats.
		const Batch *m_state = nullptr;
		Light m_light_set{};
		bool m_light_valid = false;
	};

	void D3d11Renderer::Begin(int width, int height, const render::Camera &camera)
	{
		Renderer::Begin(width, height, camera);
		m_vertices.clear();
		m_indices.clear();
		m_batches.clear();
		m_triangle_count = 0;
		m_copied = nullptr;
		++m_frame;
	}

	void D3d11Renderer::Draw(const render::Vertex *vertices, size_t vertex_count, const uint32_t *indices,
		size_t index_count, const render::Material &material, bool reuse_vertices)
	{
		if (vertex_count == 0)
			return;
		// The vertices as they are, once for all of a mesh's batches; the light is the shader's.
		if (!reuse_vertices || m_copied != vertices || m_copied_count != vertex_count) {
			m_copied = vertices;
			m_copied_count = vertex_count;
			m_copied_base = static_cast<uint32_t>(m_vertices.size());
			m_vertices.resize(m_copied_base + vertex_count);
			GpuVertex *out = m_vertices.data() + m_copied_base;
			for (size_t i = 0; i < vertex_count; ++i)
				out[i] = GpuVertex::From(vertices[i]);
		}
		AddBatch(indices, index_count, vertex_count, m_copied_base, material, nullptr, nullptr);
	}

	void D3d11Renderer::DrawStatic(const render::StaticVertices &pool, size_t first_vertex, size_t vertex_count,
		const uint32_t *indices, size_t index_count, const render::Material &material, bool reuse_vertices)
	{
		ID3D11Buffer *buffer = m.StaticBuffer(pool);
		if (!buffer || first_vertex + vertex_count > pool.vertices.size()) {
			Renderer::DrawStatic(pool, first_vertex, vertex_count, indices, index_count, material, reuse_vertices);
			return;
		}
		AddBatch(indices, index_count, vertex_count, static_cast<uint32_t>(first_vertex), material, &pool, buffer);
	}

	void D3d11Renderer::AddBatch(const uint32_t *indices, size_t index_count, size_t vertex_count, uint32_t base,
		const render::Material &material, const render::StaticVertices *pool, ID3D11Buffer *buffer)
	{
		size_t usable = index_count - index_count % 3;
		uint32_t first = static_cast<uint32_t>(m_indices.size());
		for (size_t t = 0; t < usable; t += 3) {
			if (indices[t] >= vertex_count || indices[t + 1] >= vertex_count || indices[t + 2] >= vertex_count)
				continue;
			m_indices.push_back(indices[t] + base);
			m_indices.push_back(indices[t + 1] + base);
			m_indices.push_back(indices[t + 2] + base);
		}
		uint32_t count = static_cast<uint32_t>(m_indices.size()) - first;
		if (count == 0)
			return;
		float texel_alpha = !material.texel_alpha || !material.texture ? 0.0f
		                    : material.blend == render::Blend::Translucent ? 1.0f
		                    : material.blend == render::Blend::Additive    ? 2.0f
		                                                                   : 0.0f;
		Light light = {m_light_scale.x, m_light_scale.y, m_light_scale.z, material.opacity / 255.0f,
			m_light_add.x / 255.0f, m_light_add.y / 255.0f, m_light_add.z / 255.0f, 0, m_dynamic_sun ? 1.0f : 0.0f,
			static_cast<float>(m_point_mask), material.fog ? 0.0f : 1.0f, texel_alpha, m_light_base.x / 255.0f, m_light_base.y / 255.0f,
			m_light_base.z / 255.0f, 0};
		m_batches.push_back(
			{first, count, m.TextureView(material.texture), material.blend, material.two_sided, m_background, light, pool, buffer});
		m_triangle_count += count / 3;
	}

	PassConstants D3d11Renderer::Pass(bool background, uint32_t clear_color) const
	{
		PassConstants pass{};
		SetVector(pass.eye, background ? render::Vec3{0, 0, 0} : m_camera.position);
		SetVector(pass.right, m_right * (m_focal / (m_width * 0.5f)));
		SetVector(pass.up, m_up * (m_focal / (m_height * 0.5f)));
		SetVector(pass.forward, m_forward);
		// Reversed depth, z / w = a + b / z: 1 at the near plane, 0 at the far one. The sky's
		// pass reaches past the far plane (its depth is pinned to 0 by the viewport anyway).
		float n = m_camera.near_plane, f = background ? 1e7f : std::max(m_camera.far_plane, n * 2);
		pass.depth[0] = -n / (f - n);
		pass.depth[1] = n * f / (f - n);
		SetColor(pass.fog_color, m_camera.fog_color);
		if (!background && m_camera.fog && m_camera.fog_end > m_camera.fog_start) {
			pass.fog[0] = m_camera.fog_start;
			pass.fog[1] = 1.0f / (m_camera.fog_end - m_camera.fog_start);
		}
		SetColor(pass.clear_color, clear_color);
		SetVector(pass.sun_direction, m_sun_direction);
		SetVector(pass.sun_color, m_sun_color);
		for (size_t i = 0; i < m_point_count; ++i) {
			SetVector(pass.light_position[i], m_points[i].position);
			SetVector(pass.light_strength[i], m_points[i].Strength());
		}
		return pass;
	}

	void D3d11Renderer::DrawRun(const Batch &state, uint32_t first, uint32_t count)
	{
		ID3D11DeviceContext *context = m.context.Get();
		if (!m_state || m_state->blend != state.blend || m_state->background != state.background) {
			context->PSSetShader(state.blend == render::Blend::Masked ? m.masked_ps.Get() : m.scene_ps.Get(), nullptr, 0);
			ID3D11BlendState *blend = state.blend == render::Blend::Translucent ? m.blend_alpha.Get()
			                          : state.blend == render::Blend::Additive  ? m.blend_add.Get()
			                                                                    : m.blend_opaque.Get();
			context->OMSetBlendState(blend, nullptr, 0xffffffff);
			ID3D11DepthStencilState *depth = state.background ? m.depth_background.Get()
			                                 : state.Solid()  ? m.depth_write.Get()
			                                                  : m.depth_test.Get();
			context->OMSetDepthStencilState(depth, 0);
		}
		if (!m_state || m_state->two_sided != state.two_sided)
			context->RSSetState(state.two_sided ? m.cull_none.Get() : m.cull_back.Get());
		if (!m_state || m_state->texture != state.texture)
			context->PSSetShaderResources(0, 1, &state.texture);
		if (!m_state || m_state->buffer != state.buffer) {
			UINT stride = sizeof(GpuVertex), offset = 0;
			ID3D11Buffer *buffer = state.buffer ? state.buffer : m.vertex_buffer.Get();
			context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
		}
		if (!m_light_valid || m_light_set != state.light) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(context->Map(m.batch_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				memcpy(mapped.pData, state.light.data(), sizeof(Light));
				context->Unmap(m.batch_constants.Get(), 0);
			}
			m_light_set = state.light;
			m_light_valid = true;
		}
		m_state = &state;
		context->DrawIndexed(count, first, 0);
	}

	void D3d11Renderer::Finish(uint32_t *pixels, int stride, uint32_t clear_color, bool clear)
	{
		// Where the block is in the window: the canvas shows the view through it.
		if (!m.canvas || stride != m.canvas_stride || pixels < m.canvas ||
		    pixels >= m.canvas + static_cast<size_t>(m.canvas_stride) * m.canvas_height)
			return;
		ptrdiff_t offset = pixels - m.canvas;
		int view_x = static_cast<int>(offset % m.canvas_stride), view_y = static_cast<int>(offset / m.canvas_stride);
		int visible_w = std::min(m_width, m.canvas_width - view_x), visible_h = std::min(m_height, m.canvas_height - view_y);
		m.canvas_dirty.ClearBlock(pixels, stride, view_x, view_y, visible_w, visible_h);
		if (m.lost)
			return;

		// Opaque and masked triangles grouped by material, a draw call each instead of one per
		// mesh batch (thousands, looking across a zone): the depth buffer decides what's in
		// front, so their order doesn't matter.
		size_t drawn_end = m_indices.size();
		m_grouped.clear();
		for (const Batch &batch : m_batches)
			if (!batch.background && batch.Solid())
				m_grouped.push_back(batch);
		std::stable_sort(m_grouped.begin(), m_grouped.end(), [](const Batch &a, const Batch &b) { return a.StateBefore(b); });
		size_t grouped_count = 0;
		for (const Batch &batch : m_grouped)
			grouped_count += batch.count;
		m_indices.resize(drawn_end + grouped_count);
		uint32_t next = static_cast<uint32_t>(drawn_end);
		for (Batch &batch : m_grouped) {
			std::copy_n(m_indices.begin() + batch.first, batch.count, m_indices.begin() + next);
			batch.first = next;
			next += batch.count;
		}

		// Translucent and additive triangles back to front, by their farthest vertex, after
		// everything else: the order the software rasterizer blends them in.
		m_sorted.clear();
		for (uint32_t b = 0; b < m_batches.size(); ++b) {
			const Batch &batch = m_batches[b];
			if (batch.background || batch.Solid())
				continue;
			for (uint32_t i = batch.first; i < batch.first + batch.count; i += 3) {
				float depth = -1e30f;
				for (int k = 0; k < 3; ++k)
					depth = std::max(depth, render::Dot(Position(batch, m_indices[i + k]) - m_camera.position, m_forward));
				m_sorted.push_back({depth, b, i});
			}
		}
		std::stable_sort(m_sorted.begin(), m_sorted.end(),
			[](const SortedTriangle &a, const SortedTriangle &b) { return a.depth > b.depth; });
		size_t sorted_first = m_indices.size();
		m_indices.resize(sorted_first + m_sorted.size() * 3);
		for (size_t i = 0; i < m_sorted.size(); ++i)
			std::copy_n(m_indices.begin() + m_sorted[i].first, 3, m_indices.begin() + sorted_first + i * 3);
		bool uploaded = m.Upload(m.vertex_buffer, m.vertex_capacity, D3D11_BIND_VERTEX_BUFFER, m_vertices.data(),
		                    m_vertices.size() * sizeof(GpuVertex)) &&
		                m.Upload(m.index_buffer, m.index_capacity, D3D11_BIND_INDEX_BUFFER, m_indices.data(),
		                    m_indices.size() * sizeof(uint32_t));
		m_indices.resize(drawn_end);

		ID3D11DeviceContext *context = m.context.Get();
		PassConstants scene = Pass(false, clear_color & 0xffffff), background = Pass(true, 0);
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(context->Map(m.scene_pass.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			memcpy(mapped.pData, &scene, sizeof(scene));
			context->Unmap(m.scene_pass.Get(), 0);
		}
		if (SUCCEEDED(context->Map(m.background_pass.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			memcpy(mapped.pData, &background, sizeof(background));
			context->Unmap(m.background_pass.Get(), 0);
		}

		context->OMSetRenderTargets(1, m.back_buffer.GetAddressOf(), m.depth_view.Get());
		D3D11_VIEWPORT viewport{static_cast<float>(view_x), static_cast<float>(view_y), static_cast<float>(m_width),
			static_cast<float>(m_height), 0, 1};
		context->RSSetViewports(1, &viewport);
		context->VSSetConstantBuffers(0, 1, m.scene_pass.GetAddressOf());
		context->PSSetConstantBuffers(0, 1, m.scene_pass.GetAddressOf());
		context->PSSetSamplers(0, 1, m.sampler.GetAddressOf());

		// Clear the block: depth to nothing drawn, and the color unless drawing over an earlier pass.
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(m.fullscreen_vs.Get(), nullptr, 0);
		context->PSSetShader(m.clear_ps.Get(), nullptr, 0);
		context->OMSetBlendState(clear ? m.blend_opaque.Get() : m.blend_no_color.Get(), nullptr, 0xffffffff);
		context->OMSetDepthStencilState(m.depth_always.Get(), 0);
		context->RSSetState(m.cull_none.Get());
		context->Draw(3, 0);

		if (uploaded && !m_batches.empty()) {
			context->IASetInputLayout(m.layout.Get());
			context->IASetIndexBuffer(m.index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
			context->VSSetShader(m.scene_vs.Get(), nullptr, 0);
			context->VSSetConstantBuffers(1, 1, m.batch_constants.GetAddressOf());
			m_state = nullptr;
			m_light_valid = false;

			// Runs of batches that follow on in the index buffer and share a state draw together.
			auto draw_batches = [&](const std::vector<Batch> &batches, auto wanted) {
				const Batch *run = nullptr;
				uint32_t first = 0, count = 0;
				for (const Batch &batch : batches) {
					if (!wanted(batch))
						continue;
					if (run && run->SameState(batch) && first + count == batch.first) {
						count += batch.count;
						continue;
					}
					if (run)
						DrawRun(*run, first, count);
					run = &batch;
					first = batch.first;
					count = batch.count;
				}
				if (run)
					DrawRun(*run, first, count);
			};

			// Opaque and masked, by material.
			draw_batches(m_grouped, [](const Batch &) { return true; });

			// The sky, in draw order, only where nothing was drawn: its depth is pinned to the
			// far plane's 0, which passes only against the cleared 0.
			bool sky = std::any_of(m_batches.begin(), m_batches.end(), [](const Batch &b) { return b.background; });
			if (sky) {
				D3D11_VIEWPORT far_viewport = viewport;
				far_viewport.MinDepth = far_viewport.MaxDepth = 0;
				context->RSSetViewports(1, &far_viewport);
				context->VSSetConstantBuffers(0, 1, m.background_pass.GetAddressOf());
				context->PSSetConstantBuffers(0, 1, m.background_pass.GetAddressOf());
				draw_batches(m_batches, [](const Batch &b) { return b.background; });
				context->RSSetViewports(1, &viewport);
				context->VSSetConstantBuffers(0, 1, m.scene_pass.GetAddressOf());
				context->PSSetConstantBuffers(0, 1, m.scene_pass.GetAddressOf());
			}

			// Translucent and additive, back to front.
			const Batch *run = nullptr;
			uint32_t first = 0, count = 0;
			for (size_t i = 0; i < m_sorted.size(); ++i) {
				const Batch &batch = m_batches[m_sorted[i].batch];
				if (run && run->SameState(batch)) {
					count += 3;
					continue;
				}
				if (run)
					DrawRun(*run, first, count);
				run = &batch;
				first = static_cast<uint32_t>(sorted_first + i * 3);
				count = 3;
			}
			if (run)
				DrawRun(*run, first, count);
			m_state = nullptr;
		}

		if (m_depth_wanted && m_frame - m_depth_wanted <= 2)
			CopyDepth(view_x, view_y, scene.depth[0], scene.depth[1]);
	}

	void D3d11Renderer::CopyDepth(int view_x, int view_y, float depth_a, float depth_b)
	{
		// Over the oldest copy that isn't the one being read.
		int slot = -1;
		for (int i = 0; i < DepthCopies; ++i)
			if (i != m_mapped && (slot < 0 || m_depth[i].frame < m_depth[slot].frame))
				slot = i;
		DepthCopy &copy = m_depth[slot];
		if (!copy.texture) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = static_cast<UINT>(m.width);
			desc.Height = static_cast<UINT>(m.height);
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R32_TYPELESS; // the depth buffer's own format; read as floats
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			if (FAILED(m.device->CreateTexture2D(&desc, nullptr, &copy.texture)))
				return;
		}
		m.context->CopyResource(copy.texture.Get(), m.depth.Get());
		copy.frame = m_frame;
		copy.view_x = view_x;
		copy.view_y = view_y;
		copy.width = m_width;
		copy.height = m_height;
		copy.eye = m_camera.position;
		copy.right = m_right;
		copy.up = m_up;
		copy.forward = m_forward;
		copy.focal = m_focal;
		copy.near_plane = m_camera.near_plane;
		copy.depth_a = depth_a;
		copy.depth_b = depth_b;
	}

	void D3d11Renderer::Unmap() const
	{
		if (m_mapped >= 0)
			m.context->Unmap(m_depth[m_mapped].texture.Get(), 0);
		m_mapped = -1;
	}

	const D3d11Renderer::DepthCopy *D3d11Renderer::ReadyDepth() const
	{
		m_depth_wanted = m_frame;
		if (m_depth_checked != m_frame) {
			// Once a frame: the newest copy the GPU has finished, if it's newer than the one
			// mapped, without waiting for it.
			m_depth_checked = m_frame;
			int order[DepthCopies];
			for (int i = 0; i < DepthCopies; ++i)
				order[i] = i;
			std::sort(order, order + DepthCopies, [&](int a, int b) { return m_depth[a].frame > m_depth[b].frame; });
			uint64_t have = m_mapped >= 0 ? m_depth[m_mapped].frame : 0;
			for (int i : order) {
				if (m_depth[i].frame <= have)
					break;
				D3D11_MAPPED_SUBRESOURCE mapping;
				if (SUCCEEDED(m.context->Map(m_depth[i].texture.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapping))) {
					Unmap();
					m_mapped = i;
					m_mapping = mapping;
					break;
				}
			}
		}
		return m_mapped >= 0 ? &m_depth[m_mapped] : nullptr;
	}

	bool D3d11Renderer::Occluded(const render::Vec3 &point) const
	{
		float x, y;
		if (!Project(point, x, y))
			return true;
		const DepthCopy *copy = ReadyDepth();
		if (!copy)
			return false;
		// Where the point was under the camera that drew the copy.
		render::Vec3 d = point - copy->eye;
		float z = render::Dot(d, copy->forward);
		if (z < copy->near_plane)
			return true;
		int px = static_cast<int>(copy->width * 0.5f + render::Dot(d, copy->right) / z * copy->focal);
		int py = static_cast<int>(copy->height * 0.5f - render::Dot(d, copy->up) / z * copy->focal);
		if (px < 0 || py < 0 || px >= copy->width || py >= copy->height)
			return false;
		px += copy->view_x;
		py += copy->view_y;
		if (px >= m.width || py >= m.height)
			return false;
		float stored = reinterpret_cast<const float *>(static_cast<const uint8_t *>(m_mapping.pData) +
		                                               static_cast<size_t>(py) * m_mapping.RowPitch)[px];
		if (stored <= 0)
			return false;
		// Back to 1 / z, as the software depth buffer holds: something more than 2% nearer hides the point.
		return (stored - copy->depth_a) / copy->depth_b > (1.0f / z) * 1.02f;
	}

	void D3d11Renderer::DrawEdges(uint32_t *pixels, int stride, uint32_t color) const
	{
		for (const Batch &batch : m_batches) {
			if (batch.background)
				continue;
			for (uint32_t i = batch.first; i < batch.first + batch.count; i += 3) {
				float x[3], y[3];
				bool shown = true;
				for (int k = 0; k < 3 && shown; ++k)
					shown = Project(Position(batch, m_indices[i + k]), x[k], y[k]);
				if (!shown)
					continue;
				// Front faces are clockwise on screen.
				if (!batch.two_sided && (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]) <= 0)
					continue;
				render::DrawLine(pixels, stride, m_width, m_height, x[0], y[0], x[1], y[1], color);
				render::DrawLine(pixels, stride, m_width, m_height, x[1], y[1], x[2], y[2], color);
				render::DrawLine(pixels, stride, m_width, m_height, x[2], y[2], x[0], y[0], color);
			}
		}
	}
}

D3d11Device::D3d11Device() : m(std::make_unique<State>()) {}

D3d11Device::~D3d11Device()
{
	if (m->context)
		m->context->ClearState();
}

const std::string &D3d11Device::AdapterName() const
{
	return m->adapter;
}

bool D3d11Device::Tearing() const
{
	return m->tearing;
}

bool D3d11Device::VSync() const
{
	return m->vsync;
}

void D3d11Device::SetVSync(bool on)
{
	m->vsync = on;
}

bool D3d11Device::Create(HWND window, int width, int height, bool vsync, std::string &error)
{
	m->window = window;
	m->width = width;
	m->height = height;
	m->vsync = vsync;

	// The shader compiler ships with Windows 10 and later; without it, no GPU.
	HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
	if (!compiler) {
		error = "d3dcompiler_47.dll not found";
		return false;
	}
	auto compile = reinterpret_cast<CompileFunction>(reinterpret_cast<void *>(GetProcAddress(compiler, "D3DCompile")));
	if (!compile) {
		FreeLibrary(compiler);
		error = "D3DCompile missing from d3dcompiler_47.dll";
		return false;
	}

	D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
	D3D_FEATURE_LEVEL level{};
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
		static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &m->device, &level, &m->context);
	if (FAILED(hr)) {
		FreeLibrary(compiler);
		error = "D3D11CreateDevice failed (" + HresultText(hr) + ")";
		return false;
	}

	ComPtr<IDXGIDevice> dxgi_device;
	ComPtr<IDXGIAdapter> adapter;
	if (SUCCEEDED(m->device.As(&dxgi_device)) && SUCCEEDED(dxgi_device->GetAdapter(&adapter))) {
		DXGI_ADAPTER_DESC desc{};
		if (SUCCEEDED(adapter->GetDesc(&desc)))
			m->adapter = Narrow(desc.Description);
	}
	// One frame queued ahead at most (the default is three): less input lag, and the depth
	// copies name tags read come back sooner.
	ComPtr<IDXGIDevice1> dxgi_device1;
	if (SUCCEEDED(m->device.As(&dxgi_device1)))
		dxgi_device1->SetMaximumFrameLatency(1);

	// The swap chain: flip model where the system has it (Windows 10: FLIP_DISCARD, 8: FLIP_SEQUENTIAL),
	// the old blit model otherwise.
	ComPtr<IDXGIFactory2> factory2;
	if (adapter)
		adapter->GetParent(IID_PPV_ARGS(&factory2));
	if (factory2) {
		// Without vsync, a flip model swap chain in a window still runs at the refresh rate
		// unless it may tear (Windows 10 and later). Allowed even with vsync, which the debug
		// menu can turn off.
		BOOL tearing = FALSE;
		ComPtr<IDXGIFactory5> factory5;
		if (SUCCEEDED(factory2.As(&factory5)) &&
		    FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))))
			tearing = FALSE;
		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = static_cast<UINT>(width);
		desc.Height = static_cast<UINT>(height);
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		const DXGI_SWAP_EFFECT effects[] = {DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_SWAP_EFFECT_DISCARD};
		for (DXGI_SWAP_EFFECT effect : effects) {
			bool flip = effect != DXGI_SWAP_EFFECT_DISCARD;
			desc.SwapEffect = effect;
			desc.BufferCount = flip ? 2 : 1;
			desc.Flags = flip && tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
			ComPtr<IDXGISwapChain1> chain;
			if (SUCCEEDED(factory2->CreateSwapChainForHwnd(m->device.Get(), window, &desc, nullptr, nullptr, &chain))) {
				m->swap_chain = chain;
				m->tearing = desc.Flags != 0;
				break;
			}
		}
		factory2->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
	}
	else if (adapter) {
		ComPtr<IDXGIFactory> factory;
		if (SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
			DXGI_SWAP_CHAIN_DESC desc{};
			desc.BufferDesc.Width = static_cast<UINT>(width);
			desc.BufferDesc.Height = static_cast<UINT>(height);
			desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.BufferCount = 1;
			desc.OutputWindow = window;
			desc.Windowed = TRUE;
			desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
			factory->CreateSwapChain(m->device.Get(), &desc, &m->swap_chain);
			factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
		}
	}
	if (!m->swap_chain) {
		FreeLibrary(compiler);
		error = "couldn't create a swap chain";
		return false;
	}

	ComPtr<ID3D11Texture2D> back;
	if (FAILED(m->swap_chain->GetBuffer(0, IID_PPV_ARGS(&back))) ||
	    FAILED(m->device->CreateRenderTargetView(back.Get(), nullptr, &m->back_buffer))) {
		FreeLibrary(compiler);
		error = "couldn't get the swap chain's back buffer";
		return false;
	}

	D3D11_TEXTURE2D_DESC depth_desc{};
	depth_desc.Width = static_cast<UINT>(width);
	depth_desc.Height = static_cast<UINT>(height);
	depth_desc.MipLevels = 1;
	depth_desc.ArraySize = 1;
	depth_desc.Format = DXGI_FORMAT_R32_TYPELESS; // D32_FLOAT to draw, floats when copied back
	depth_desc.SampleDesc.Count = 1;
	depth_desc.Usage = D3D11_USAGE_DEFAULT;
	depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	D3D11_DEPTH_STENCIL_VIEW_DESC depth_view{};
	depth_view.Format = DXGI_FORMAT_D32_FLOAT;
	depth_view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	if (FAILED(m->device->CreateTexture2D(&depth_desc, nullptr, &m->depth)) ||
	    FAILED(m->device->CreateDepthStencilView(m->depth.Get(), &depth_view, &m->depth_view))) {
		FreeLibrary(compiler);
		error = "couldn't create the depth buffer";
		return false;
	}

	// Shaders, compiled now for shader model 4 (feature level 10.0 and up).
	auto build = [&](const char *entry, const char *target, ComPtr<ID3DBlob> &code) {
		ComPtr<ID3DBlob> messages;
		HRESULT result = compile(ShaderSource, sizeof(ShaderSource) - 1, "my-client", nullptr, nullptr, entry, target,
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages);
		if (FAILED(result)) {
			error = std::string("shader ") + entry + ": " +
			        (messages ? std::string(static_cast<const char *>(messages->GetBufferPointer()), messages->GetBufferSize())
			                  : HresultText(result));
			return false;
		}
		return true;
	};
	ComPtr<ID3DBlob> scene_vs, fullscreen_vs, scene_ps, masked_ps, clear_ps, composite_ps;
	bool compiled = build("SceneVS", "vs_4_0", scene_vs) && build("FullscreenVS", "vs_4_0", fullscreen_vs) &&
	                build("ScenePS", "ps_4_0", scene_ps) && build("MaskedPS", "ps_4_0", masked_ps) &&
	                build("ClearPS", "ps_4_0", clear_ps) && build("CompositePS", "ps_4_0", composite_ps);
	if (compiled) {
		ID3D11Device *device = m->device.Get();
		const D3D11_INPUT_ELEMENT_DESC layout[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
		};
		compiled =
			SUCCEEDED(device->CreateVertexShader(scene_vs->GetBufferPointer(), scene_vs->GetBufferSize(), nullptr, &m->scene_vs)) &&
			SUCCEEDED(device->CreateVertexShader(fullscreen_vs->GetBufferPointer(), fullscreen_vs->GetBufferSize(), nullptr,
				&m->fullscreen_vs)) &&
			SUCCEEDED(device->CreatePixelShader(scene_ps->GetBufferPointer(), scene_ps->GetBufferSize(), nullptr, &m->scene_ps)) &&
			SUCCEEDED(device->CreatePixelShader(masked_ps->GetBufferPointer(), masked_ps->GetBufferSize(), nullptr, &m->masked_ps)) &&
			SUCCEEDED(device->CreatePixelShader(clear_ps->GetBufferPointer(), clear_ps->GetBufferSize(), nullptr, &m->clear_ps)) &&
			SUCCEEDED(device->CreatePixelShader(composite_ps->GetBufferPointer(), composite_ps->GetBufferSize(), nullptr,
				&m->composite_ps)) &&
			SUCCEEDED(device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)), scene_vs->GetBufferPointer(),
				scene_vs->GetBufferSize(), &m->layout));
		if (!compiled && error.empty())
			error = "couldn't create the shaders";
	}
	scene_vs.Reset();
	fullscreen_vs.Reset();
	scene_ps.Reset();
	masked_ps.Reset();
	clear_ps.Reset();
	composite_ps.Reset();
	FreeLibrary(compiler);
	if (!compiled)
		return false;

	ID3D11Device *device = m->device.Get();
	bool made = true;

	D3D11_BUFFER_DESC pass{};
	pass.ByteWidth = sizeof(PassConstants);
	pass.Usage = D3D11_USAGE_DYNAMIC;
	pass.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	pass.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_BUFFER_DESC batch = pass;
	batch.ByteWidth = 64;
	made = made && SUCCEEDED(device->CreateBuffer(&pass, nullptr, &m->scene_pass)) &&
	       SUCCEEDED(device->CreateBuffer(&pass, nullptr, &m->background_pass)) &&
	       SUCCEEDED(device->CreateBuffer(&batch, nullptr, &m->batch_constants));

	// Bilinear with mipmaps, like eqgfx's D3DTEXF_LINEAR, repeating.
	D3D11_SAMPLER_DESC sampler{};
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	made = made && SUCCEEDED(device->CreateSamplerState(&sampler, &m->sampler));

	auto blend_state = [&](bool enable, D3D11_BLEND source, D3D11_BLEND dest, UINT8 write_mask, ComPtr<ID3D11BlendState> &out) {
		D3D11_BLEND_DESC desc{};
		D3D11_RENDER_TARGET_BLEND_DESC &target = desc.RenderTarget[0];
		target.BlendEnable = enable;
		target.SrcBlend = source;
		target.DestBlend = dest;
		target.BlendOp = D3D11_BLEND_OP_ADD;
		target.SrcBlendAlpha = D3D11_BLEND_ONE;
		target.DestBlendAlpha = D3D11_BLEND_ZERO;
		target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		target.RenderTargetWriteMask = write_mask;
		return SUCCEEDED(device->CreateBlendState(&desc, &out));
	};
	const UINT8 all = D3D11_COLOR_WRITE_ENABLE_ALL;
	made = made && blend_state(false, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, all, m->blend_opaque) &&
	       blend_state(true, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, all, m->blend_alpha) &&
	       blend_state(true, D3D11_BLEND_ONE, D3D11_BLEND_ONE, all, m->blend_add) &&
	       blend_state(false, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, 0, m->blend_no_color) &&
	       // canvas + view * its top byte
	       blend_state(true, D3D11_BLEND_ONE, D3D11_BLEND_SRC_ALPHA, all, m->blend_composite);

	auto depth_state = [&](D3D11_COMPARISON_FUNC test, bool write, ComPtr<ID3D11DepthStencilState> &out) {
		D3D11_DEPTH_STENCIL_DESC desc{};
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = test;
		return SUCCEEDED(device->CreateDepthStencilState(&desc, &out));
	};
	made = made && depth_state(D3D11_COMPARISON_GREATER, true, m->depth_write) &&
	       depth_state(D3D11_COMPARISON_GREATER, false, m->depth_test) &&
	       depth_state(D3D11_COMPARISON_GREATER_EQUAL, false, m->depth_background) &&
	       depth_state(D3D11_COMPARISON_ALWAYS, true, m->depth_always);

	// WLD polygons face the camera when they come out clockwise on screen.
	auto rasterizer_state = [&](D3D11_CULL_MODE cull, ComPtr<ID3D11RasterizerState> &out) {
		D3D11_RASTERIZER_DESC desc{};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = cull;
		desc.FrontCounterClockwise = FALSE;
		desc.DepthClipEnable = TRUE;
		return SUCCEEDED(device->CreateRasterizerState(&desc, &out));
	};
	made = made && rasterizer_state(D3D11_CULL_BACK, m->cull_back) && rasterizer_state(D3D11_CULL_NONE, m->cull_none);

	// Untextured materials sample white.
	uint32_t white = 0xffffffff;
	D3D11_TEXTURE2D_DESC white_desc{};
	white_desc.Width = white_desc.Height = 1;
	white_desc.MipLevels = white_desc.ArraySize = 1;
	white_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	white_desc.SampleDesc.Count = 1;
	white_desc.Usage = D3D11_USAGE_IMMUTABLE;
	white_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA white_data{&white, 4, 4};
	ComPtr<ID3D11Texture2D> white_texture;
	made = made && SUCCEEDED(device->CreateTexture2D(&white_desc, &white_data, &white_texture)) &&
	       SUCCEEDED(device->CreateShaderResourceView(white_texture.Get(), nullptr, &m->white));

	D3D11_TEXTURE2D_DESC canvas_desc = white_desc;
	canvas_desc.Width = static_cast<UINT>(width);
	canvas_desc.Height = static_cast<UINT>(height);
	canvas_desc.Usage = D3D11_USAGE_DEFAULT; // updated a band at a time (UpdateSubresource)
	canvas_desc.CPUAccessFlags = 0;
	made = made && SUCCEEDED(device->CreateTexture2D(&canvas_desc, nullptr, &m->canvas_texture)) &&
	       SUCCEEDED(device->CreateShaderResourceView(m->canvas_texture.Get(), nullptr, &m->canvas_view));

	if (!made) {
		error = "couldn't create the pipeline states";
		return false;
	}
	return true;
}

void D3d11Device::SetCanvas(uint32_t *pixels, int width, int height, int stride)
{
	m->canvas = pixels;
	m->canvas_width = std::min(width, m->width);
	m->canvas_height = std::min(height, m->height);
	m->canvas_stride = stride;
	m->canvas_dirty.Resize(m->canvas_width, m->canvas_height);
}

CanvasDirty *D3d11Device::CanvasDrawn()
{
	return &m->canvas_dirty;
}

std::unique_ptr<render::Renderer> D3d11Device::MakeRenderer()
{
	return std::make_unique<D3d11Renderer>(*m);
}

void D3d11Device::BeginFrame()
{
	if (m->lost)
		return;
	const float black[4] = {0, 0, 0, 1};
	m->context->ClearRenderTargetView(m->back_buffer.Get(), black);
	m->context->ClearDepthStencilView(m->depth_view.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
}

double D3d11Device::UploadMs() const
{
	return m->upload_ms;
}

bool D3d11Device::Present()
{
	if (m->lost)
		return false;
	ID3D11DeviceContext *context = m->context.Get();

	auto upload_start = std::chrono::steady_clock::now();
	if (m->canvas) {
		// What was drawn on this frame or the last (cleared since), a run of tiles at a time.
		m->canvas_dirty.ForEachRun([&](int x, int y, int w, int h) {
			D3D11_BOX box{static_cast<UINT>(x), static_cast<UINT>(y), 0, static_cast<UINT>(x + w), static_cast<UINT>(y + h), 1};
			context->UpdateSubresource(m->canvas_texture.Get(), 0, &box,
				m->canvas + static_cast<size_t>(y) * m->canvas_stride + x, static_cast<UINT>(m->canvas_stride) * 4, 0);
		});
		m->canvas_dirty.NextFrame();
	}
	m->upload_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - upload_start).count();

	context->OMSetRenderTargets(1, m->back_buffer.GetAddressOf(), nullptr);
	D3D11_VIEWPORT viewport{0, 0, static_cast<float>(m->width), static_cast<float>(m->height), 0, 1};
	context->RSSetViewports(1, &viewport);
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(m->fullscreen_vs.Get(), nullptr, 0);
	context->PSSetShader(m->composite_ps.Get(), nullptr, 0);
	context->PSSetShaderResources(0, 1, m->canvas_view.GetAddressOf());
	context->OMSetBlendState(m->blend_composite.Get(), nullptr, 0xffffffff);
	context->OMSetDepthStencilState(nullptr, 0);
	context->RSSetState(m->cull_none.Get());
	context->Draw(3, 0);

	HRESULT hr = m->swap_chain->Present(m->vsync ? 1 : 0, !m->vsync && m->tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
		m->lost = true;
		return false;
	}
	if (hr == DXGI_STATUS_OCCLUDED)
		Sleep(16); // minimized: nothing is shown, don't spin
	return true;
}
