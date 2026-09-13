/**************************************************************************/
/*  csg_bevel_modifier.h                                                  */
/**************************************************************************/

#pragma once

#include "csg_modifier.h"

class CSGBevelModifier : public CSGModifier {
	GDCLASS(CSGBevelModifier, CSGModifier);

public:
	enum ApplicationMode {
		APPLICATION_MODE_OPERANDS,
		APPLICATION_MODE_RESULT,
		APPLICATION_MODE_BOTH,
	};

private:
	real_t width = 0.1;
	real_t angle = Math::deg_to_rad(30.0);
	ApplicationMode application_mode = APPLICATION_MODE_OPERANDS;

	void _process_brush(CSGBrush *p_brush) const;

protected:
	static void _bind_methods();

public:
	void set_width(real_t p_width);
	real_t get_width() const;

	void set_angle(real_t p_angle);
	real_t get_angle() const;

	void set_application_mode(ApplicationMode p_mode);
	ApplicationMode get_application_mode() const;

	virtual uint32_t get_process_stages() const override;
	virtual void process(const Ref<CSGModifierContext> &p_context) override;
};

VARIANT_ENUM_CAST(CSGBevelModifier::ApplicationMode);
