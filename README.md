# Report Rate Limit

A ZMK module that provides rate limiting for pointer input reports and runtime rate limit adjustment.

## Overview

> [!CAUTION]
> LLM-assisted README (overseen). 

Slightly modified version of [@badjeff's](https://github.com/badjeff) module that provides both input processor rate limiting and runtime behavior for adjusting report rates. This module helps control pointer movement frequency and provides user-configurable rate limiting with feedback.

## Components

The module consists of two main components:

1. **Input Processor**: Limits the frequency of pointer reports
2. **Behavior**: Allows runtime adjustment of rate limits with feedback

## Input Processor

### Device Tree Binding

```dts
#include <input/processors.dtsi>

/ {
    input-processors {
        zip_report_rate_limit: report_rate_limit {
            compatible = "zmk,input-processor-report-rate-limit";
            #processing-cells = <0>;
        };
    };
};
```

### Usage in Trackball Configuration

```dts
trackball {
    pointer {
        input-processors = <&zip_report_rate_limit>;
    };
};
```

## Rate Limit Behavior

### Device Tree Binding

```dts
#include <behaviors/rate_limit.dtsi>

/ {
    behaviors {
        rrl: rate_limit {
            compatible = "zmk,behavior-rate-limit";
            #binding-cells = <1>;
            values-ms = <8 6 4 2>;
            feedback-duration = <65>;
        };
    };
};
```

### Configuration Options

- **values-ms**: Array of rate limit values in milliseconds (required)
- **feedback-gpios**: GPIO for visual feedback (optional)
- **feedback-extra-gpios**: Power GPIO (optional)
- **feedback-duration**: Duration of feedback pulse in milliseconds (default: 0)
- **feedback-wrap-pattern**: Custom pattern for wrap-around feedback (optional)

### Behavior Parameters

- **param1**: Direction (1 = increase, -1 = decrease)

## Usage Examples

### Basic Rate Limit Cycling

In the endgame-trackball configuration:

```dts
behaviors {
    rrl {
        values-ms = <8 6 4 2>;
        feedback-duration = <65>;
    };
};

// In keymap
bindings = <
    &rrl 1  // Cycle to next rate limit
>;
```

### With LED Feedback

```dts
behaviors {
    rrl_with_led {
        compatible = "zmk,behavior-rate-limit";
        #binding-cells = <1>;
        values-ms = <16 12 8 4 2>;
        feedback-gpios = <&gpio0 24 GPIO_ACTIVE_HIGH>;
        feedback-duration = <100>;
    };
};
```

### With Wrap Pattern

```dts
behaviors {
    rrl_pattern {
        compatible = "zmk,behavior-rate-limit";
        #binding-cells = <1>;
        values-ms = <16 8 4>;
        feedback-gpios = <&gpio0 24 GPIO_ACTIVE_HIGH>;
        feedback-wrap-pattern = <100 50 100 50 200>;  // Custom blink pattern
        feedback-duration = <50>;
    };
};
```

