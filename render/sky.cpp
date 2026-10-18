#include "sky.h"

namespace render
{
	bool Sky::Load(const std::string &eq_dir, int type, std::string &error)
	{
		m_layers.clear();
		m_base_uvs.clear();
		std::vector<uint8_t> data;
		WldFile wld;
		if (!m_archive.Open(eq_dir + "/sky.s3d") || !m_archive.Read("sky.wld", data) || !wld.Load(std::move(data))) {
			error = "Couldn't read sky.s3d";
			return false;
		}
		m_textures.AddArchive(&m_archive);

		for (int layer : {1, 3}) {
			WldMesh mesh;
			std::string name = "LAYER" + std::to_string(type) + std::to_string(layer) + "_DMSPRITEDEF";
			if (!wld.ReadMesh(wld.Find(name), mesh))
				continue;
			StaticMesh out;
			std::vector<float> uvs;
			for (size_t i = 0; i < mesh.positions.size(); ++i) {
				Vertex v;
				// Scaled up so the dome sits well past the near plane.
				v.position = Vec3{mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z} * 100.0f;
				v.u = mesh.uvs[i * 2];
				v.v = mesh.uvs[i * 2 + 1];
				out.vertices.push_back(v);
				uvs.push_back(v.u);
				uvs.push_back(v.v);
			}
			out.radius = 100;
			for (uint16_t m = 0; m < mesh.materials.size(); ++m) {
				const WldMaterial &source = mesh.materials[m];
				MaterialInfo info = DescribeRenderMethod(source.render_method);
				StaticMesh::Batch batch;
				// The dome draws opaque (render method 0x0d); clouds blend over it.
				batch.material.blend = layer == 1 ? Blend::Opaque : info.blend;
				batch.material.opacity = info.opacity;
				batch.material.two_sided = true;
				for (const auto &texture : source.textures)
					if (const Texture *t = m_textures.Get(texture, info.masked))
						batch.frames.push_back(t);
				for (const auto &t : mesh.triangles)
					if (t.material == m)
						batch.indices.insert(batch.indices.end(), {t.v[0], t.v[1], t.v[2]});
				if (layer == 1 && !batch.frames.empty())
					m_horizon = batch.frames[0]->levels.back()[0] & 0xffffff; // the dome's average color
				out.batches.push_back(std::move(batch));
			}
			m_layers.push_back(std::move(out));
			m_base_uvs.push_back(std::move(uvs));
		}
		if (m_layers.empty()) {
			error = "sky type " + std::to_string(type) + " not in sky.s3d";
			return false;
		}
		return true;
	}

	void Sky::Draw(Renderer &renderer, uint64_t now_ms)
	{
		for (size_t i = 0; i < m_layers.size(); ++i) {
			StaticMesh &layer = m_layers[i];
			if (i > 0) {
				// Clouds drift a texture width every couple of minutes.
				float drift = static_cast<float>(now_ms % 120000) / 120000.0f;
				for (size_t v = 0; v < layer.vertices.size(); ++v)
					layer.vertices[v].u = m_base_uvs[i][v * 2] + drift;
			}
			DrawStaticMesh(renderer, layer, now_ms);
		}
	}
}
