// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//##########################################################################

#include "Drawings.h"
#include "DxfWriter.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
	const char* LAYER_FOOTPRINT = "FOOTPRINT";
	const char* LAYER_WALLS     = "WALLS";
	const char* LAYER_DIMS      = "DIMENSIONS";
	const char* LAYER_TEXT      = "TEXT";

	std::string fmt(double v, const QString& unit)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.2f %s", v, unit.toUtf8().constData());
		return std::string(buf);
	}

	double textHeightFor(double span)
	{
		return std::max(0.1, span * 0.03);
	}
}

namespace Drawings
{
	QString writePlan(const BuildingDims::Result& dims, const Planes::Model& model, const QString& path)
	{
		if (!dims.valid)
			return QStringLiteral("Invalid dimensions; nothing to draw.");

		DxfWriter dxf;
		dxf.addLayer(LAYER_FOOTPRINT, 5);   // blue
		dxf.addLayer(LAYER_WALLS, 3);       // green
		dxf.addLayer(LAYER_DIMS, 1);        // red
		dxf.addLayer(LAYER_TEXT, 7);

		const double th = textHeightFor(std::max(dims.length, dims.width));

		// --- footprint polygon (global XY) --------------------------------------
		std::vector<DxfWriter::Pt> fp;
		for (const CCVector3d& c : dims.footprint)
			fp.push_back({ c.x, c.y });
		dxf.addPolyline(LAYER_FOOTPRINT, fp, true);

		// --- wall segments (each vertical plane projected to XY) ----------------
		for (const Planes::Plane& pl : model.planes)
		{
			if (pl.type != Planes::Type::Wall)
				continue;
			// horizontal in-plane direction = normal x Z
			CCVector3d h(pl.normal.y * 1.0 - 0.0, 0.0 - pl.normal.x * 1.0, 0.0);  // normal × (0,0,1)
			const double hn = std::sqrt(h.x * h.x + h.y * h.y);
			if (hn < 1e-9)
				continue;
			h.x /= hn;
			h.y /= hn;
			const double half = pl.inPlaneWidth * 0.5;
			const double x1 = pl.centroidGlobal.x - h.x * half;
			const double y1 = pl.centroidGlobal.y - h.y * half;
			const double x2 = pl.centroidGlobal.x + h.x * half;
			const double y2 = pl.centroidGlobal.y + h.y * half;
			dxf.addLine(LAYER_WALLS, x1, y1, x2, y2);
		}

		// --- overall dimension lines (length along fp[0]->fp[1], width fp[1]->fp[2])
		const CCVector3d& p0 = dims.footprint[0];
		const CCVector3d& p1 = dims.footprint[1];
		const CCVector3d& p2 = dims.footprint[2];
		const double off = std::max(dims.length, dims.width) * 0.08 + th * 2.0;
		dxf.addAlignedDim(LAYER_DIMS, p0.x, p0.y, p1.x, p1.y, -off, fmt(dims.length, dims.unit), th);
		dxf.addAlignedDim(LAYER_DIMS, p1.x, p1.y, p2.x, p2.y, off, fmt(dims.width, dims.unit), th);

		// --- title --------------------------------------------------------------
		dxf.addText(LAYER_TEXT, p0.x, p0.y - off * 1.8, th * 1.4,
		            std::string("PLAN  H=") + fmt(dims.height, dims.unit)
		                + "  storeys=" + std::to_string(model.storeyCount));

		return dxf.write(path.toStdString()) ? QString()
		                                      : QStringLiteral("Failed to write plan DXF: %1").arg(path);
	}

	QString writeElevations(const Planes::Model& model, const QString& unit, const QString& path)
	{
		DxfWriter dxf;
		dxf.addLayer(LAYER_WALLS, 3);
		dxf.addLayer(LAYER_DIMS, 1);
		dxf.addLayer(LAYER_TEXT, 7);

		double cursorX   = 0.0;
		int    elevIndex = 0;
		double maxSpan   = 1.0;
		for (const Planes::Plane& pl : model.planes)
			if (pl.type == Planes::Type::Wall)
				maxSpan = std::max(maxSpan, std::max(pl.inPlaneWidth, pl.inPlaneHeight));
		const double th  = textHeightFor(maxSpan);
		const double gap = maxSpan * 0.4 + th * 6.0;

		for (const Planes::Plane& pl : model.planes)
		{
			if (pl.type != Planes::Type::Wall)
				continue;

			const double w = pl.inPlaneWidth;
			const double h = pl.inPlaneHeight;
			const double x0 = cursorX;

			// wall outline rectangle
			std::vector<DxfWriter::Pt> rect = {
				{ x0, 0.0 }, { x0 + w, 0.0 }, { x0 + w, h }, { x0, h }
			};
			dxf.addPolyline(LAYER_WALLS, rect, true);

			// width dim (below) + height dim (left)
			const double off = th * 3.0;
			dxf.addAlignedDim(LAYER_DIMS, x0, 0.0, x0 + w, 0.0, -off, fmt(w, unit), th);
			dxf.addAlignedDim(LAYER_DIMS, x0, h, x0, 0.0, -off, fmt(h, unit), th);

			dxf.addText(LAYER_TEXT, x0, h + th * 1.5, th * 1.2,
			            std::string("ELEVATION ") + std::to_string(elevIndex + 1));

			cursorX += w + gap;
			++elevIndex;
		}

		if (elevIndex == 0)
			return QStringLiteral("No wall planes detected for elevations.");

		return dxf.write(path.toStdString()) ? QString()
		                                      : QStringLiteral("Failed to write elevation DXF: %1").arg(path);
	}
}
