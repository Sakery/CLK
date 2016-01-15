//
//  ElectronDocument.swift
//  Clock Signal
//
//  Created by Thomas Harte on 03/01/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

import Foundation
import AudioToolbox

class ElectronDocument: MachineDocument {

	private var electron = CSElectron()
	override init() {
		super.init()
		self.intendedCyclesPerSecond = 2000000

		if let osPath = NSBundle.mainBundle().pathForResource("os", ofType: "rom") {
			electron.setOSROM(NSData(contentsOfFile: osPath)!)
		}
		if let basicPath = NSBundle.mainBundle().pathForResource("basic", ofType: "rom") {
			electron.setBASICROM(NSData(contentsOfFile: basicPath)!)
		}
	}

	override func windowControllerDidLoadNib(aController: NSWindowController) {
		super.windowControllerDidLoadNib(aController)
		electron.view = openGLView
		electron.audioQueue = self.audioQueue
		openGLView.frameBounds = CGRectMake(0.0225, 0.0625, 0.75, 0.75)
	}

	override var windowNibName: String? {
		return "ElectronDocument"
	}

	override func readFromData(data: NSData, ofType typeName: String) throws {
		electron.setROM(data, slot: 15)
	}

	// MARK: CSOpenGLViewDelegate
	override func runForNumberOfCycles(numberOfCycles: Int32) {
		electron.runForNumberOfCycles(numberOfCycles)
	}

	// MARK: CSOpenGLViewResponderDelegate
	override func keyDown(event: NSEvent) {
		electron.setKey(event.keyCode, isPressed: true)
	}

	override func keyUp(event: NSEvent) {
		electron.setKey(event.keyCode, isPressed: false)
	}

	override func flagsChanged(newModifiers: NSEvent) {
		electron.setKey(kVK_Shift, isPressed: newModifiers.modifierFlags.contains(.ShiftKeyMask))
		electron.setKey(kVK_Control, isPressed: newModifiers.modifierFlags.contains(.ControlKeyMask))
		electron.setKey(kVK_Command, isPressed: newModifiers.modifierFlags.contains(.CommandKeyMask))
	}

}
