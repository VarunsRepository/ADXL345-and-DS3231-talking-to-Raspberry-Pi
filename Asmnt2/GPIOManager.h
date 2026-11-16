#ifndef GPIOMANAGER_H
#define GPIOMANAGER_H

#include <gpiod.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Wrapper around libgpiod v2. Holds one chip plus up to one input request
// and one output request, so a program can monitor edges on some pins and
// drive others through a single object.
//
// Owns kernel resources, so it follows the rule of five: copying is
// forbidden (two objects releasing the same request would be a double
// free) and no move is provided because nothing here needs one.
class GPIOManager {
public:
    // Throws std::runtime_error if the chip cannot be opened. Throwing
    // rather than calling exit() lets the caller report the problem and
    // still run its own cleanup.
    explicit GPIOManager(const std::string& chipPath,
                         const std::string& consumer = "een1071");
    ~GPIOManager() noexcept;

    GPIOManager(const GPIOManager&) = delete;
    GPIOManager& operator=(const GPIOManager&) = delete;

    // Falling-edge detection with pull-up bias, so buttons wire to ground.
    // Throws std::runtime_error if the lines cannot be requested - usually
    // because another process already holds them (check with `gpioinfo`).
    void configureInputs(const std::vector<unsigned int>& pins);

    // Outputs, all starting low. Same failure behaviour as above.
    void configureOutputs(const std::vector<unsigned int>& pins);

    // Sets one output line. `pin` is the GPIO offset, exactly as passed to
    // configureOutputs - libgpiod's set_value takes an offset, not an index
    // into the request, so no pin-to-index map is needed.
    // Returns false if the line is not part of the output request.
    bool writePin(unsigned int pin, bool value);

    // Sets several output lines in one ioctl. Every pin must belong to the
    // output request.
    bool writePins(const std::vector<unsigned int>& pins,
                   const std::vector<bool>& values);

    // Blocks until an edge arrives. The timeout is in NANOSECONDS (the
    // libgpiod v2 unit) and is int64_t, so values beyond ~2.1 s are
    // representable. Negative means wait indefinitely.
    // Returns 1 if an event is pending, 0 on timeout, -1 on error.
    int waitForEvent(std::int64_t timeout_ns = -1);

    // Moves pending events into the internal buffer.
    // Returns the number read, or -1 on error. Never reads more than the
    // buffer's capacity.
    int readEvents(std::size_t max_events = kEventBufferCapacity);

    // Offset of the event at `index` within the last readEvents() batch.
    // Returns false if the index is out of range, instead of handing a
    // null event to libgpiod.
    bool getEventPin(std::size_t index, unsigned int& pinOut);

    static constexpr std::size_t kEventBufferCapacity = 16;

private:
    std::string chip_path;   // owned copy: a const char* member could dangle
    std::string consumer;    // shows up in `gpioinfo` against each line

    gpiod_chip* chip = nullptr;
    gpiod_line_request* input_request = nullptr;
    gpiod_line_request* output_request = nullptr;
    gpiod_edge_event_buffer* event_buffer = nullptr;

    std::vector<unsigned int> input_pins;
    std::vector<unsigned int> output_pins;
};

#endif
