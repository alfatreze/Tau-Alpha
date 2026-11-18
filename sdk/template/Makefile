# One command builds the meter:  make          (-> dist/<id>.tmeter)
#                         test:  make test     (the conformance suite, with a picture: preview.png, then test/test_meter.py)
#                    thumbnail:  make thumb    (rewrites thumb.json from a frame of your meter)
# The SDK (tau-alpha-meter-sdk) is looked for in $(TAU_METER_SDK), then ../tau-alpha-meter-sdk (this folder copied next to it), then .. (this folder left inside the SDK as template/), then on PATH as tau-meter.
SDK ?= $(firstword $(wildcard $(TAU_METER_SDK) ../tau-alpha-meter-sdk) $(patsubst %/,%,$(dir $(wildcard ../tau-meter))))
TAU_METER ?= $(if $(SDK),python3 $(SDK)/tau-meter,tau-meter)

.PHONY: all test clean thumb
all:
	$(TAU_METER) build
test: all
	$(TAU_METER) test dist/*.tmeter --preview preview.png
	python3 test/test_meter.py $(SDK)
thumb:
	$(TAU_METER) thumb .
clean:
	rm -rf dist build preview.png
