#include "GPIOManager.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

// libgpiod's settings/config/request-config objects are heap allocated and
// the caller must free them. They are only needed until the request is
// made, so each is wrapped in a unique_ptr: the frees then happen on every
// path, including the ones where we throw.
struct LineSettingsDeleter {
    void operator()(gpiod_line_settings* p) const {
        if (p) gpiod_line_settings_free(p);
    }
};
struct LineConfigDeleter {
    void operator()(gpiod_line_config* p) const {
        if (p) gpiod_line_config_free(p);
    }
};
struct RequestConfigDeleter {
    void operator()(gpiod_request_config* p) const {
        if (p) gpiod_request_config_free(p);
    }
};

using SettingsPtr = std::unique_ptr<gpiod_line_settings, LineSettingsDeleter>;
using LineConfigPtr = std::unique_ptr<gpiod_line_config, LineConfigDeleter>;
using RequestConfigPtr = std::unique_ptr<gpiod_request_config, RequestConfigDeleter>;

} /* anonymous namespace */

GPIOManager::GPIOManager(const std::string& chipPath, const std::string& consumerName)
    : chip_path(chipPath), consumer(consumerName)
{
    chip = gpiod_chip_open(chip_path.c_str());
    if (!chip) {
        throw std::runtime_error("GPIOManager: cannot open chip " + chip_path);
    }

    event_buffer = gpiod_edge_event_buffer_new(kEventBufferCapacity);
    if (!event_buffer) {
        gpiod_chip_close(chip);       // the destructor will not run: we are
        chip = nullptr;               // still inside the constructor
        throw std::runtime_error("GPIOManager: cannot allocate event buffer");
    }
}

GPIOManager::~GPIOManager() noexcept
{
    // All four of these tolerate a null argument in libgpiod 2.x, but the
    // checks cost nothing and do not rely on that staying true.
    if (event_buffer)   gpiod_edge_event_buffer_free(event_buffer);
    if (input_request)  gpiod_line_request_release(input_request);
    if (output_request) gpiod_line_request_release(output_request);
    if (chip)           gpiod_chip_close(chip);
}

void GPIOManager::configureInputs(const std::vector<unsigned int>& pins)
{
    if (pins.empty()) {
        throw std::runtime_error("GPIOManager: configureInputs needs at least one pin");
    }

    SettingsPtr settings(gpiod_line_settings_new());
    LineConfigPtr cfg(gpiod_line_config_new());
    RequestConfigPtr req(gpiod_request_config_new());
    if (!settings || !cfg || !req) {
        throw std::runtime_error("GPIOManager: out of memory configuring inputs");
    }

    gpiod_line_settings_set_direction(settings.get(), GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings.get(), GPIOD_LINE_EDGE_FALLING);
    gpiod_line_settings_set_bias(settings.get(), GPIOD_LINE_BIAS_PULL_UP);

    if (gpiod_line_config_add_line_settings(cfg.get(), pins.data(), pins.size(),
                                            settings.get()) != 0) {
        throw std::runtime_error("GPIOManager: invalid input line configuration");
    }

    gpiod_request_config_set_consumer(req.get(), consumer.c_str());

    // Releasing any previous request first: overwriting the handle would
    // leak it and leave the old lines claimed.
    if (input_request) {
        gpiod_line_request_release(input_request);
        input_request = nullptr;
    }

    input_request = gpiod_chip_request_lines(chip, req.get(), cfg.get());
    if (!input_request) {
        throw std::runtime_error("GPIOManager: cannot request input lines "
                                 "(already in use? check gpioinfo)");
    }

    input_pins = pins;
}

void GPIOManager::configureOutputs(const std::vector<unsigned int>& pins)
{
    if (pins.empty()) {
        throw std::runtime_error("GPIOManager: configureOutputs needs at least one pin");
    }

    SettingsPtr settings(gpiod_line_settings_new());
    LineConfigPtr cfg(gpiod_line_config_new());
    RequestConfigPtr req(gpiod_request_config_new());
    if (!settings || !cfg || !req) {
        throw std::runtime_error("GPIOManager: out of memory configuring outputs");
    }

    gpiod_line_settings_set_direction(settings.get(), GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings.get(), GPIOD_LINE_VALUE_INACTIVE);

    if (gpiod_line_config_add_line_settings(cfg.get(), pins.data(), pins.size(),
                                            settings.get()) != 0) {
        throw std::runtime_error("GPIOManager: invalid output line configuration");
    }

    gpiod_request_config_set_consumer(req.get(), consumer.c_str());

    if (output_request) {
        gpiod_line_request_release(output_request);
        output_request = nullptr;
    }

    output_request = gpiod_chip_request_lines(chip, req.get(), cfg.get());
    if (!output_request) {
        throw std::runtime_error("GPIOManager: cannot request output lines "
                                 "(already in use? check gpioinfo)");
    }

    output_pins = pins;
}

bool GPIOManager::writePin(unsigned int pin, bool value)
{
    if (!output_request) {
        return false;
    }

    const int rc = gpiod_line_request_set_value(
        output_request,
        pin,                              // offset, not an index
        value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);

    return rc == 0;
}

bool GPIOManager::writePins(const std::vector<unsigned int>& pins,
                            const std::vector<bool>& values)
{
    if (!output_request || pins.empty() || pins.size() != values.size()) {
        return false;
    }

    // std::vector<bool> is a bitfield, so it is copied into a real array
    // rather than passed through as contiguous enum storage.
    std::vector<gpiod_line_value> raw(pins.size());
    for (std::size_t i = 0; i < pins.size(); ++i) {
        raw[i] = values[i] ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
    }

    return gpiod_line_request_set_values_subset(output_request, pins.size(),
                                                pins.data(), raw.data()) == 0;
}

int GPIOManager::waitForEvent(std::int64_t timeout_ns)
{
    if (!input_request) {
        return -1;
    }
    return gpiod_line_request_wait_edge_events(input_request, timeout_ns);
}

int GPIOManager::readEvents(std::size_t max_events)
{
    if (!input_request) {
        return -1;
    }

    // The buffer must be able to hold whatever we ask for.
    const std::size_t wanted = std::min(max_events, kEventBufferCapacity);
    if (wanted == 0) {
        return 0;
    }

    return gpiod_line_request_read_edge_events(input_request, event_buffer, wanted);
}

bool GPIOManager::getEventPin(std::size_t index, unsigned int& pinOut)
{
    if (!event_buffer) {
        return false;
    }

    // Returns null (with EINVAL) once index passes the number of events
    // actually stored, so this must be checked before using the event.
    gpiod_edge_event* event = gpiod_edge_event_buffer_get_event(event_buffer, index);
    if (!event) {
        return false;
    }

    pinOut = gpiod_edge_event_get_line_offset(event);
    return true;
}
