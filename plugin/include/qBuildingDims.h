// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                                                                        #
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//#                                                                        #
//#  Extracts building dimensions (length/width/height) from a point       #
//#  cloud, exports the footprint to DXF, and registers a -BUILDING_DIMS    #
//#  command-line command for AI-agent / MCP integration.                  #
//#                                                                        #
//#  Distributed under the terms of the GNU GPL v3 (see LICENSE).          #
//#                                                                        #
//##########################################################################

#pragma once

#include "ccStdPluginInterface.h"

//! Building dimension extraction plugin (Standard).
class qBuildingDims : public QObject, public ccStdPluginInterface
{
	Q_OBJECT
	Q_INTERFACES(ccPluginInterface ccStdPluginInterface)
	Q_PLUGIN_METADATA(IID "cccorp.cloudcompare.plugin.qBuildingDims" FILE "../info.json")

public:
	explicit qBuildingDims(QObject* parent = nullptr);
	~qBuildingDims() override = default;

	// Inherited from ccStdPluginInterface
	void          onNewSelection(const ccHObject::Container& selectedEntities) override;
	QList<QAction*> getActions() override;

	// Inherited from ccPluginInterface: register CLI commands.
	void registerCommands(ccCommandLineInterface* cmd) override;

private:
	//! Runs extraction on the currently selected cloud (GUI path).
	void doAction();

	QAction* m_action = nullptr;
};
