// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//##########################################################################

#include "Openings.h"

#include <ccPointCloud.h>

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	QString typeName(Openings::Type t)
	{
		switch (t)
		{
		case Openings::Type::Door:   return QStringLiteral("door");
		case Openings::Type::Window: return QStringLiteral("window");
		default:                     return QStringLiteral("unknown");
		}
	}

	//! Grid helpers on a nu×nv occupancy raster (row-major, cv*nu + cu).
	inline int idx(int cu, int cv, int nu) { return cv * nu + cu; }
}

namespace Openings
{
	std::vector<Opening> detect(const ccPointCloud* cloud, const Planes::Model& model, const Params& params)
	{
		std::vector<Opening> result;
		if (!cloud)
			return result;

		const double scale    = (cloud->getGlobalScale() != 0.0) ? cloud->getGlobalScale() : 1.0;
		const double invScale = 1.0 / scale;
		const double cellGlobal = (params.cellSize > 0.0) ? params.cellSize : 0.08;
		const double cellLocal  = cellGlobal * scale;

		int wallIndex = -1;
		for (const Planes::Plane& pl : model.planes)
		{
			// Elevation index counts only walls, matching Drawings.
			if (pl.type != Planes::Type::Wall)
				continue;
			++wallIndex;

			const double wLocal = pl.uMax - pl.uMin;
			const double hLocal = pl.vMax - pl.vMin;
			if (wLocal <= 0.0 || hLocal <= 0.0 || cellLocal <= 0.0)
				continue;

			int nu = std::clamp(static_cast<int>(std::ceil(wLocal / cellLocal)), 1, params.maxGridCells);
			int nv = std::clamp(static_cast<int>(std::ceil(hLocal / cellLocal)), 1, params.maxGridCells);
			if (nu < 3 || nv < 3)
				continue;  // too coarse to host an interior void

			const double du = wLocal / nu;
			const double dv = hLocal / nv;

			// --- occupancy raster ------------------------------------------------
			std::vector<char> occ(static_cast<size_t>(nu) * nv, 0);
			for (unsigned pi : pl.inliers)
			{
				const CCVector3* q = cloud->getPoint(pi);
				const CCVector3d dq(q->x - pl.centroidLocal.x,
				                    q->y - pl.centroidLocal.y,
				                    q->z - pl.centroidLocal.z);
				const double pu = dq.dot(pl.axisU) - pl.uMin;
				const double pv = dq.dot(pl.axisV) - pl.vMin;
				int cu = std::clamp(static_cast<int>(pu / du), 0, nu - 1);
				int cv = std::clamp(static_cast<int>(pv / dv), 0, nv - 1);
				occ[idx(cu, cv, nu)] = 1;
			}

			// --- flood "outside" from the border through EMPTY cells -------------
			std::vector<char> outside(static_cast<size_t>(nu) * nv, 0);
			std::vector<int>  stack;
			auto pushIfEmpty = [&](int cu, int cv) {
				if (cu < 0 || cv < 0 || cu >= nu || cv >= nv)
					return;
				const int k = idx(cu, cv, nu);
				if (!occ[k] && !outside[k])
				{
					outside[k] = 1;
					stack.push_back(k);
				}
			};
			for (int cu = 0; cu < nu; ++cu) { pushIfEmpty(cu, 0); pushIfEmpty(cu, nv - 1); }
			for (int cv = 0; cv < nv; ++cv) { pushIfEmpty(0, cv); pushIfEmpty(nu - 1, cv); }
			while (!stack.empty())
			{
				const int k = stack.back(); stack.pop_back();
				const int cu = k % nu, cv = k / nu;
				pushIfEmpty(cu - 1, cv); pushIfEmpty(cu + 1, cv);
				pushIfEmpty(cu, cv - 1); pushIfEmpty(cu, cv + 1);
			}

			// --- connected components of INTERIOR empty cells --------------------
			std::vector<char> visited(static_cast<size_t>(nu) * nv, 0);
			for (int cv = 0; cv < nv; ++cv)
			{
				for (int cu = 0; cu < nu; ++cu)
				{
					const int start = idx(cu, cv, nu);
					if (occ[start] || outside[start] || visited[start])
						continue;

					// BFS this void component; track its cell bounding box.
					int cu0 = cu, cu1 = cu, cv0 = cv, cv1 = cv;
					std::vector<int> comp{ start };
					visited[start] = 1;
					for (size_t s = 0; s < comp.size(); ++s)
					{
						const int kk = comp[s];
						const int x = kk % nu, y = kk / nu;
						cu0 = std::min(cu0, x); cu1 = std::max(cu1, x);
						cv0 = std::min(cv0, y); cv1 = std::max(cv1, y);
						const int nb[4][2] = { { x - 1, y }, { x + 1, y }, { x, y - 1 }, { x, y + 1 } };
						for (auto& e : nb)
						{
							if (e[0] < 0 || e[1] < 0 || e[0] >= nu || e[1] >= nv)
								continue;
							const int kn = idx(e[0], e[1], nu);
							if (!occ[kn] && !outside[kn] && !visited[kn])
							{
								visited[kn] = 1;
								comp.push_back(kn);
							}
						}
					}

					// --- candidate rectangle -> global sizes ---------------------
					const double u0 = pl.uMin + cu0 * du;
					const double u1 = pl.uMin + (cu1 + 1) * du;
					const double v0 = pl.vMin + cv0 * dv;
					const double v1 = pl.vMin + (cv1 + 1) * dv;
					const double wG = (u1 - u0) * invScale;
					const double hG = (v1 - v0) * invScale;
					if (wG < params.minWidth || hG < params.minHeight)
						continue;

					// --- frame occupancy (one-cell ring around the bbox) ---------
					int frameTotal = 0, frameOcc = 0;
					for (int x = cu0 - 1; x <= cu1 + 1; ++x)
					{
						for (int y = cv0 - 1; y <= cv1 + 1; ++y)
						{
							const bool onRing = (x == cu0 - 1 || x == cu1 + 1 || y == cv0 - 1 || y == cv1 + 1);
							if (!onRing || x < 0 || y < 0 || x >= nu || y >= nv)
								continue;
							++frameTotal;
							if (occ[idx(x, y, nu)])
								++frameOcc;
						}
					}
					const double conf = (frameTotal > 0) ? static_cast<double>(frameOcc) / frameTotal : 0.0;
					if (conf < params.minFrameOcc)
						continue;

					Opening op;
					op.wallIndex  = wallIndex;
					op.width      = wG;
					op.height     = hG;
					op.sillHeight = (v0 - pl.vMin) * invScale;
					op.confidence = conf;
					op.u0 = u0 - pl.uMin; op.u1 = u1 - pl.uMin;
					op.v0 = v0 - pl.vMin; op.v1 = v1 - pl.vMin;
					op.type = (op.sillHeight <= params.doorSillMax) ? Type::Door : Type::Window;
					result.push_back(op);
				}
			}
		}

		return result;
	}

	QJsonArray toJson(const std::vector<Opening>& openings, const QString& unit)
	{
		QJsonArray arr;
		for (const Opening& o : openings)
		{
			QJsonObject j;
			j[QStringLiteral("wall_index")]  = o.wallIndex;
			j[QStringLiteral("type")]        = typeName(o.type);
			j[QStringLiteral("width")]       = o.width;
			j[QStringLiteral("height")]      = o.height;
			j[QStringLiteral("sill_height")] = o.sillHeight;
			j[QStringLiteral("confidence")]  = o.confidence;
			j[QStringLiteral("unit")]        = unit;
			arr.append(j);
		}
		return arr;
	}
}
