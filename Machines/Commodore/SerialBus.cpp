//
//  SerialPort.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 05/07/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#include "SerialBus.hpp"

using namespace Commodore::Serial;

const char *::Commodore::Serial::StringForLine(Line line)
{
	switch(line)
	{
		case ServiceRequest: return "Service request";
		case Attention: return "Attention";
		case Clock: return "Clock";
		case Data: return "Data";
		case Reset: return "Reset";
	}
}

void Bus::add_port(std::shared_ptr<Port> port)
{
	_ports.push_back(port);
	for(int line = (int)ServiceRequest; line <= (int)Reset; line++)
	{
		// the addition of a new device may change the line output...
		set_line_output_did_change((Line)line);

		// ... but the new device will need to be told the current state regardless
		port->set_input((Line)line, _line_values[line]);
	}
}

void Bus::set_line_output_did_change(Line line)
{
	// i.e. I believe these lines to be open collector, active low
	bool new_line_value = false;
	for(std::weak_ptr<Port> port : _ports)
	{
		std::shared_ptr<Port> locked_port = port.lock();
		if(locked_port)
		{
			new_line_value |= locked_port->get_output(line);
		}
	}

	// post an update only if one occurred
	if(new_line_value != _line_values[line])
	{
		printf("[Bus] %s is %s\n", StringForLine(line), new_line_value ? "true" : "false");
		_line_values[line] = new_line_value;

		for(std::weak_ptr<Port> port : _ports)
		{
			std::shared_ptr<Port> locked_port = port.lock();
			if(locked_port)
			{
				locked_port->set_input(line, new_line_value);
			}
		}
	}
}
