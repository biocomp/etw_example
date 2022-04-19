# etw_example
Event Tracing for Windows log example

A minimal example of a data log implemented via Event Tracing for Windows.
Using [EventRegister](https://docs.microsoft.com/en-us/windows/win32/api/evntprov/nf-evntprov-eventregister)/
[EventWrite](https://docs.microsoft.com/en-us/windows/win32/api/evntprov/nf-evntprov-eventwrite) to produce events, and write them into the .etl file.

ETW consists of three major components: Providers, Controllers and Consumers.
Providers produce events, controllers create and control event sessions, and consumers consume the events.

In this example, MinoLog is Producer + Controller in one package, and the test is a consumer of the events via .etl file.
