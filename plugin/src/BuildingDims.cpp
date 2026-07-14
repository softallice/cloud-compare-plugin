// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                                                                        #
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//#                                                                        #
//##########################################################################

#include "BuildingDims.h"

// qCC_db
#include <ccPointCloud.h>
#include <ccPolyline.h>
#include <ccHObject.h>

// qCC_io
#include <FileIOFilter.h>

// Qt
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	//! Unique ID of CloudCompare's bundled DXF output filter (see DxfFilter.cpp).
	constexpr const char* DXF_FILTER_ID = "_DXF Filter";

	QJsonArray vec3ToJson(const CCVector3d& v)
	{
		return QJsonArray{ v.x, v.y, v.z };
	}
}

namespace BuildingDims
{
	Result compute(const ccPointCloud* cloud, Method method, const QString& unit)
	{
		Result res;
		res.method = method;
		res.unit   = unit;

		if (!cloud || cloud->size() == 0)
		{
			res.warnings << QStringLiteral("Empty or null point cloud.");
			return res;
		}

		const unsigned n = cloud->size();
		res.pointCount    = n;
		res.globalShift   = cloud->getGlobalShift();
		res.globalScale   = cloud->getGlobalScale();
		const double invScale = (res.globalScale != 0.0) ? 1.0 / res.globalScale : 1.0;

		// --- vertical extent (Z) -------------------------------------------------
		double zMin = std::numeric_limits<double>::max();
		double zMax = std::numeric_limits<double>::lowest();

		// --- XY centroid ---------------------------------------------------------
		double sx = 0.0, sy = 0.0;
		for (unsigned i = 0; i < n; ++i)
		{
			const CCVector3* P = cloud->getPoint(i);
			sx += P->x;
			sy += P->y;
			zMin = std::min(zMin, static_cast<double>(P->z));
			zMax = std::max(zMax, static_cast<double>(P->z));
		}
		const double cx = sx / n;
		const double cy = sy / n;

		// height is a length -> only the scale matters, shift cancels.
		res.height = (zMax - zMin) * invScale;

		// --- horizontal principal axes ------------------------------------------
		CCVector3d u(1.0, 0.0, 0.0);  // major (length) axis
		CCVector3d v(0.0, 1.0, 0.0);  // minor (width)  axis

		if (method == Method::OBB)
		{
			// 2x2 covariance of the XY footprint.
			double cxx = 0.0, cxy = 0.0, cyy = 0.0;
			for (unsigned i = 0; i < n; ++i)
			{
				const CCVector3* P = cloud->getPoint(i);
				const double dx = P->x - cx;
				const double dy = P->y - cy;
				cxx += dx * dx;
				cxy += dx * dy;
				cyy += dy * dy;
			}
			cxx /= n;
			cxy /= n;
			cyy /= n;

			// Closed-form eigenvector angle of a symmetric 2x2 matrix.
			const double theta = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);
			u = CCVector3d(std::cos(theta), std::sin(theta), 0.0);
			v = CCVector3d(-std::sin(theta), std::cos(theta), 0.0);
		}

		// Project points onto (u, v) to get extents (local units).
		double uMin = std::numeric_limits<double>::max();
		double uMax = std::numeric_limits<double>::lowest();
		double vMin = std::numeric_limits<double>::max();
		double vMax = std::numeric_limits<double>::lowest();
		for (unsigned i = 0; i < n; ++i)
		{
			const CCVector3* P = cloud->getPoint(i);
			const double dx = P->x - cx;
			const double dy = P->y - cy;
			const double pu = dx * u.x + dy * u.y;
			const double pv = dx * v.x + dy * v.y;
			uMin = std::min(uMin, pu);
			uMax = std::max(uMax, pu);
			vMin = std::min(vMin, pv);
			vMax = std::max(vMax, pv);
		}

		double extU = uMax - uMin;  // local
		double extV = vMax - vMin;  // local

		// Ensure length >= width; swap axes/extents if needed.
		if (extV > extU)
		{
			std::swap(extU, extV);
			std::swap(u, v);
			std::swap(uMin, vMin);
			std::swap(uMax, vMax);
		}

		res.length     = extU * invScale;
		res.width      = extV * invScale;
		res.axisLength = u;
		res.axisWidth  = v;

		// --- footprint rectangle (local -> global) ------------------------------
		auto localCorner = [&](double a, double b) -> CCVector3
		{
			const double x = cx + a * u.x + b * v.x;
			const double y = cy + a * u.y + b * v.y;
			return CCVector3(static_cast<PointCoordinateType>(x),
			                 static_cast<PointCoordinateType>(y),
			                 static_cast<PointCoordinateType>(zMin));
		};

		const CCVector3 c0 = localCorner(uMin, vMin);
		const CCVector3 c1 = localCorner(uMax, vMin);
		const CCVector3 c2 = localCorner(uMax, vMax);
		const CCVector3 c3 = localCorner(uMin, vMax);

		res.footprint[0] = cloud->toGlobal3d(c0);
		res.footprint[1] = cloud->toGlobal3d(c1);
		res.footprint[2] = cloud->toGlobal3d(c2);
		res.footprint[3] = cloud->toGlobal3d(c3);
		res.center       = cloud->toGlobal3d(CCVector3(static_cast<PointCoordinateType>(cx),
		                                               static_cast<PointCoordinateType>(cy),
		                                               static_cast<PointCoordinateType>(zMin)));

		if (extU <= 0.0 || extV <= 0.0)
		{
			res.warnings << QStringLiteral("Degenerate footprint (zero extent).");
		}

		res.valid = (extU > 0.0 && res.height >= 0.0);
		return res;
	}

	ccPolyline* buildFootprintPolyline(const Result& result, const ccPointCloud* referenceCloud)
	{
		if (!result.valid || !referenceCloud)
		{
			return nullptr;
		}

		auto* vertices = new ccPointCloud(QStringLiteral("footprint.vertices"));
		if (!vertices->reserve(4))
		{
			delete vertices;
			return nullptr;
		}

		// Convert the global footprint back into the reference cloud's local frame
		// so the polyline overlaps the source cloud in the 3D view.
		for (const CCVector3d& g : result.footprint)
		{
			const CCVector3d local = referenceCloud->toLocal3d(g);
			vertices->addPoint(CCVector3(static_cast<PointCoordinateType>(local.x),
			                             static_cast<PointCoordinateType>(local.y),
			                             static_cast<PointCoordinateType>(local.z)));
		}

		auto* poly = new ccPolyline(vertices);
		poly->addChild(vertices);
		vertices->setEnabled(false);

		if (!poly->reserve(4))
		{
			delete poly;
			return nullptr;
		}
		poly->addPointIndex(0, 4);
		poly->setClosed(true);
		poly->setName(QStringLiteral("Building footprint"));

		// Carry the global shift/scale so DXF export writes real-world coordinates.
		poly->setGlobalShift(referenceCloud->getGlobalShift());
		poly->setGlobalScale(referenceCloud->getGlobalScale());
		return poly;
	}

	QByteArray toJson(const Result& result, const QString& sourceName)
	{
		QJsonObject dims;
		dims[QStringLiteral("length")] = result.length;
		dims[QStringLiteral("width")]  = result.width;
		dims[QStringLiteral("height")] = result.height;

		QJsonArray footprint;
		for (const CCVector3d& g : result.footprint)
		{
			footprint.append(QJsonArray{ g.x, g.y });
		}

		QJsonArray warnings;
		for (const QString& w : result.warnings)
		{
			warnings.append(w);
		}

		QJsonObject root;
		if (!sourceName.isEmpty())
		{
			root[QStringLiteral("source")] = sourceName;
		}
		root[QStringLiteral("ok")]           = result.valid;
		root[QStringLiteral("unit")]         = result.unit;
		root[QStringLiteral("method")]       = (result.method == Method::OBB)
		                                           ? QStringLiteral("OBB")
		                                           : QStringLiteral("AABB");
		root[QStringLiteral("dimensions")]   = dims;
		root[QStringLiteral("footprint")]    = footprint;
		root[QStringLiteral("center")]       = vec3ToJson(result.center);
		root[QStringLiteral("global_shift")] = vec3ToJson(result.globalShift);
		root[QStringLiteral("global_scale")] = result.globalScale;
		root[QStringLiteral("point_count")]  = static_cast<double>(result.pointCount);
		root[QStringLiteral("warnings")]     = warnings;

		return QJsonDocument(root).toJson(QJsonDocument::Indented);
	}

	bool writeJson(const Result& result, const QString& path, const QString& sourceName)
	{
		QFile f(path);
		if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			return false;
		}
		const QByteArray data = toJson(result, sourceName);
		const bool ok = (f.write(data) == data.size());
		f.close();
		return ok;
	}

	QString exportDxf(ccHObject* entities, const QString& path)
	{
		if (!entities)
		{
			return QStringLiteral("Nothing to export.");
		}

		FileIOFilter::SaveParameters params;
		params.alwaysDisplaySaveDialog = false;

		const CC_FILE_ERROR err = FileIOFilter::SaveToFile(entities,
		                                                   path,
		                                                   params,
		                                                   QString::fromUtf8(DXF_FILTER_ID));
		if (err != CC_FERR_NO_ERROR)
		{
			return QStringLiteral("DXF export failed (error code %1).").arg(static_cast<int>(err));
		}
		return QString();
	}
}
