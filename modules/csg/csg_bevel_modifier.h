/**************************************************************************/
/*  csg_bevel_modifier.h                                                  */
/**************************************************************************/

#pragma once

#include "csg_modifier.h"

class CSGBevelModifier : public CSGModifier {
	GDCLASS(CSGBevelModifier, CSGModifier);

	real_t width = 0.1;
	real_t angle = Math::deg_to_rad(30.0);

	void _process_brush(CSGBrush *p_brush) const;

protected:
	static void _bind_methods();

public:
	void set_width(real_t p_width);
	real_t get_width() const;

	void set_angle(real_t p_angle);
	real_t get_angle() const;

	virtual void process(const Ref<CSGModifierContext> &p_context) override;
};
