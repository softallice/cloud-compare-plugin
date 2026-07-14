// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                                                                        #
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//#                                                                        #
//##########################################################################

#include "qBuildingDims.h"

#include "BuildingDims.h"
#include "qBuildingDimsCommand.h"

// CCPluginAPI
#include <ccMainAppInterface.h>

// qCC_db
#include <ccHObjectCaster.h>
#include <ccPointCloud.h>
#include <ccPolyline.h>

// Qt
#include <QAction>
#include <QMessageBox>

qBuildingDims::qBuildingDims(QObject* parent)
    : QObject(parent)
    , ccStdPluginInterface(":/CC/plugin/qBuildingDims/info.json")
{
}

void qBuildingDims::onNewSelection(const ccHObject::Container& selectedEntities)
{
	if (!m_action)
	{
		return;
	}

	// Enable only when exactly one point cloud is selected.
	bool oneCloud = (selectedEntities.size() == 1)
	                && selectedEntities.front()->isA(CC_TYPES::POINT_CLOUD);
	m_action->setEnabled(oneCloud);
}

QList<QAction*> qBuildingDims::getActions()
{
	if (!m_action)
	{
		m_action = new QAction(getName(), this);
		m_action->setToolTip(getDescription());
		m_action->setIcon(getIcon());
		connect(m_action, &QAction::triggered, this, &qBuildingDims::doAction);
	}
	return { m_action };
}

void qBuildingDims::doAction()
{
	if (!m_app)
	{
		return;
	}

	const ccHObject::Container& selected = m_app->getSelectedEntities();
	if (selected.size() != 1 || !selected.front()->isA(CC_TYPES::POINT_CLOUD))
	{
		m_app->dispToConsole(QStringLiteral("[qBuildingDims] Select a single point cloud."),
		                     ccMainAppInterface::ERR_CONSOLE_MESSAGE);
		return;
	}

	auto* cloud = ccHObjectCaster::ToPointCloud(selected.front());
	BuildingDims::Result res = BuildingDims::compute(cloud, BuildingDims::Method::OBB);

	if (!res.valid)
	{
		m_app->dispToConsole(QStringLiteral("[qBuildingDims] Extraction failed."),
		                     ccMainAppInterface::ERR_CONSOLE_MESSAGE);
		return;
	}

	// Add the footprint polyline to the DB tree next to the source cloud.
	if (ccPolyline* poly = BuildingDims::buildFootprintPolyline(res, cloud))
	{
		poly->setColor(ccColor::yellow);
		poly->showColors(true);
		cloud->addChild(poly);
		m_app->addToDB(poly);
	}

	const QString msg = QStringLiteral(
	                        "Building dimensions (%1)\n\n"
	                        "  Length : %2 %5\n"
	                        "  Width  : %3 %5\n"
	                        "  Height : %4 %5\n\n"
	                        "Points : %6")
	                        .arg(QStringLiteral("OBB"))
	                        .arg(res.length, 0, 'f', 3)
	                        .arg(res.width, 0, 'f', 3)
	                        .arg(res.height, 0, 'f', 3)
	                        .arg(res.unit)
	                        .arg(res.pointCount);

	m_app->dispToConsole(msg, ccMainAppInterface::STD_CONSOLE_MESSAGE);
	QMessageBox::information(m_app->getMainWindow(), QStringLiteral("Building Dimensions"), msg);
}

void qBuildingDims::registerCommands(ccCommandLineInterface* cmd)
{
	if (!cmd)
	{
		return;
	}
	cmd->registerCommand(ccCommandLineInterface::Command::Shared(new CommandBuildingDims));
}
