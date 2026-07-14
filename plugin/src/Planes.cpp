// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//##########################################################################

#include "Planes.h"

#include <ccPointCloud.h>

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <random>

namespace
{
	constexpr double VERTICAL_NZ   = 0.85;  //!< |nz| above -> horizontal plane
	constexpr double HORIZONTAL_NZ = 0.30;  //!< |nz| below -> vertical (wall)

	QString typeName(Planes::Type t)
	{
		switch (t)
		{
		case Planes::Type::Wall:  return QStringLiteral("wall");
		case Planes::Type::Floor: return QStringLiteral("floor");
		case Planes::Type::Roof:  return QStringLiteral("roof");
		default:                  return QStringLiteral("other");
		}
	}
}

namespace Planes
{
	Model detect(const ccPointCloud* cloud, const Params& params)
	{
		Model model;
		if (!cloud || cloud->size() < 3)
			return model;

		const unsigned n = cloud->size();
		const double   invScale = (cloud->getGlobalScale() != 0.0) ? 1.0 / cloud->getGlobalScale() : 1.0;

		// Auto distance threshold from bbox diagonal.
		double dist = params.distThreshold;
		if (dist <= 0.0)
		{
			const ccBBox bb = const_cast<ccPointCloud*>(cloud)->getOwnBB();
			dist = 0.005 * bb.getDiagNormd();
			if (dist <= 0.0)
				dist = 0.02;
		}

		const unsigned minInliers =
		    std::max<unsigned>(50, static_cast<unsigned>(params.minInlierFrac * n));

		// Working set of still-unassigned point indices.
		std::vector<unsigned> remaining(n);
		for (unsigned i = 0; i < n; ++i)
			remaining[i] = i;

		std::mt19937 rng(params.seed);

		for (int p = 0; p < params.maxPlanes && remaining.size() >= minInliers; ++p)
		{
			std::uniform_int_distribution<size_t> pick(0, remaining.size() - 1);

			std::vector<unsigned> bestInliers;
			CCVector3d            bestNormal;
			CCVector3d            bestPoint;

			for (unsigned it = 0; it < params.ransacIters; ++it)
			{
				const CCVector3* a = cloud->getPoint(remaining[pick(rng)]);
				const CCVector3* b = cloud->getPoint(remaining[pick(rng)]);
				const CCVector3* c = cloud->getPoint(remaining[pick(rng)]);

				CCVector3d ab(b->x - a->x, b->y - a->y, b->z - a->z);
				CCVector3d ac(c->x - a->x, c->y - a->y, c->z - a->z);
				CCVector3d nrm = ab.cross(ac);
				const double norm = nrm.norm();
				if (norm < 1e-9)
					continue;
				nrm /= norm;

				const CCVector3d pa(a->x, a->y, a->z);

				std::vector<unsigned> inliers;
				inliers.reserve(remaining.size() / 4);
				for (unsigned idx : remaining)
				{
					const CCVector3* q = cloud->getPoint(idx);
					const double d = std::abs(nrm.x * (q->x - pa.x) + nrm.y * (q->y - pa.y)
					                          + nrm.z * (q->z - pa.z));
					if (d <= dist)
						inliers.push_back(idx);
				}

				if (inliers.size() > bestInliers.size())
				{
					bestInliers = std::move(inliers);
					bestNormal  = nrm;
					bestPoint   = pa;
				}
			}

			if (bestInliers.size() < minInliers)
				break;

			// --- build the plane record -----------------------------------------
			Plane plane;
			plane.normal      = bestNormal;
			plane.inlierCount = static_cast<unsigned>(bestInliers.size());
			plane.inliers     = bestInliers;

			// centroid (local) + in-plane extents
			CCVector3d cLocal(0, 0, 0);
			for (unsigned idx : bestInliers)
			{
				const CCVector3* q = cloud->getPoint(idx);
				cLocal += CCVector3d(q->x, q->y, q->z);
			}
			cLocal /= static_cast<double>(bestInliers.size());

			// Two in-plane axes.
			CCVector3d ref = (std::abs(bestNormal.z) < 0.9) ? CCVector3d(0, 0, 1)
			                                                : CCVector3d(1, 0, 0);
			CCVector3d u = bestNormal.cross(ref);
			u /= (u.norm() > 1e-9 ? u.norm() : 1.0);
			CCVector3d v = bestNormal.cross(u);
			v /= (v.norm() > 1e-9 ? v.norm() : 1.0);
			// Keep the vertical axis pointing up so vMin=base, vMax=top.
			if (v.z < 0.0)
				v = -v;

			double uMin = 1e300, uMax = -1e300, vMin = 1e300, vMax = -1e300;
			for (unsigned idx : bestInliers)
			{
				const CCVector3* q = cloud->getPoint(idx);
				CCVector3d dq(q->x - cLocal.x, q->y - cLocal.y, q->z - cLocal.z);
				const double pu = dq.dot(u);
				const double pv = dq.dot(v);
				uMin = std::min(uMin, pu); uMax = std::max(uMax, pu);
				vMin = std::min(vMin, pv); vMax = std::max(vMax, pv);
			}
			plane.inPlaneWidth  = (uMax - uMin) * invScale;
			plane.inPlaneHeight = (vMax - vMin) * invScale;
			plane.centroidLocal = cLocal;
			plane.axisU = u;
			plane.axisV = v;
			plane.uMin = uMin; plane.uMax = uMax;
			plane.vMin = vMin; plane.vMax = vMax;

			const CCVector3d cGlobal = cloud->toGlobal3d(
			    CCVector3(static_cast<PointCoordinateType>(cLocal.x),
			              static_cast<PointCoordinateType>(cLocal.y),
			              static_cast<PointCoordinateType>(cLocal.z)));
			plane.centroidGlobal = cGlobal;
			plane.elevationZ     = cGlobal.z;

			// classify
			const double nz = std::abs(bestNormal.z);
			if (nz >= VERTICAL_NZ)
				plane.type = (cGlobal.z <= 0.0) ? Type::Floor : Type::Roof;  // refined below
			else if (nz <= HORIZONTAL_NZ)
				plane.type = Type::Wall;
			else
				plane.type = Type::Other;

			model.planes.push_back(std::move(plane));

			// Remove inliers from the working set.
			std::vector<char> isInlier(n, 0);
			for (unsigned idx : bestInliers)
				isInlier[idx] = 1;
			std::vector<unsigned> next;
			next.reserve(remaining.size() - bestInliers.size());
			for (unsigned idx : remaining)
				if (!isInlier[idx])
					next.push_back(idx);
			remaining.swap(next);
		}

		// --- refine floor/roof by relative elevation + estimate storeys ----------
		std::vector<double> horizZ;
		for (Plane& pl : model.planes)
		{
			if (std::abs(pl.normal.z) >= VERTICAL_NZ)
				horizZ.push_back(pl.elevationZ);
		}
		if (!horizZ.empty())
		{
			const double zMin = *std::min_element(horizZ.begin(), horizZ.end());
			const double zMax = *std::max_element(horizZ.begin(), horizZ.end());
			const double mid  = 0.5 * (zMin + zMax);
			for (Plane& pl : model.planes)
			{
				if (std::abs(pl.normal.z) >= VERTICAL_NZ)
					pl.type = (pl.elevationZ <= mid) ? Type::Floor : Type::Roof;
			}

			// storeys: cluster horizontal elevations by minStoreyGap.
			std::sort(horizZ.begin(), horizZ.end());
			std::vector<double> levels;
			std::vector<double> gaps;
			levels.push_back(horizZ.front());
			for (size_t i = 1; i < horizZ.size(); ++i)
			{
				const double gap = horizZ[i] - levels.back();
				if (gap >= params.minStoreyGap)
				{
					gaps.push_back(gap);
					levels.push_back(horizZ[i]);
				}
			}
			model.storeyCount = std::max(1, static_cast<int>(levels.size()) - 1);
			if (!gaps.empty())
			{
				std::sort(gaps.begin(), gaps.end());
				model.storeyHeight = gaps[gaps.size() / 2];  // median gap
			}
		}

		return model;
	}

	QJsonObject toJson(const Model& model, const QString& unit)
	{
		QJsonArray planes;
		for (const Plane& pl : model.planes)
		{
			QJsonObject o;
			o[QStringLiteral("type")]   = typeName(pl.type);
			o[QStringLiteral("normal")] = QJsonArray{ pl.normal.x, pl.normal.y, pl.normal.z };
			o[QStringLiteral("center")] =
			    QJsonArray{ pl.centroidGlobal.x, pl.centroidGlobal.y, pl.centroidGlobal.z };
			o[QStringLiteral("width")]        = pl.inPlaneWidth;
			o[QStringLiteral("height")]       = pl.inPlaneHeight;
			o[QStringLiteral("inlier_count")] = static_cast<double>(pl.inlierCount);
			planes.append(o);
		}

		QJsonObject root;
		root[QStringLiteral("unit")]          = unit;
		root[QStringLiteral("plane_count")]   = planes.size();
		root[QStringLiteral("planes")]        = planes;
		root[QStringLiteral("storey_count")]  = model.storeyCount;
		root[QStringLiteral("storey_height")] = model.storeyHeight;
		return root;
	}
}
