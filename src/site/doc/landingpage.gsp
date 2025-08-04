<div class="row flex-xl-nowrap">
    <main class="col-12 col-md-12 col-xl-12 pl-md-12" role="main">
        <div class="bg-light p-5 rounded">
            <h1>UART2ETH - Serial to Network Bridge</h1>
            <p class="lead">
                RPI RP2350 based hardware and firmware solution to bridge UART interfaces to TCP sockets
            </p>
            <p>UART2ETH provides a cost-effective, open-source solution for connecting legacy industrial equipment and embedded systems to modern networks. The device enables transparent UART-over-TCP communication for up to 4 independent serial interfaces.</p>
            
            <div class="text-center mb-4">
                <a href="arc42/chapters/00_architecture_communication_canvas.html" class="btn btn-primary btn-lg" role="button">
                    📖 View Complete Architecture Documentation
                </a>
            </div>
        </div>

        <div class="row row-cols-1 row-cols-md-3 mb-3 text-center">
            <div class="col">
                <div class="card mb-4 shadow-sm">
                    <div class="card-header">
                        <h4 class="my-0 fw-normal">Multi-UART TCP Bridge</h4>
                    </div>
                    <div class="card-body">
                        <p>Up to 4 independent UART interfaces exposed as individual TCP sockets on dedicated ports. Supports concurrent connections with transparent data transmission for seamless integration with existing industrial systems.</p>
                        <ul class="list-unstyled">
                            <li>• 500kBaud sustained throughput</li>
                            <li>• Sub-5ms latency</li>
                            <li>• Full Bridge and Gateway modes</li>
                        </ul>
                    </div>
                </div>
            </div>
            <div class="col">
                <div class="card mb-4 shadow-sm">
                    <div class="card-header">
                        <h4 class="my-0 fw-normal">Secure OTA Updates</h4>
                    </div>
                    <div class="card-body">
                        <p>Over-The-Air firmware updates with A/B partition mechanism ensure continuous operation and rollback capability. Remote deployment without physical access to field-installed devices.</p>
                        <ul class="list-unstyled">
                            <li>• Secure update verification</li>
                            <li>• Automatic rollback on failure</li>
                            <li>• Zero-downtime deployment</li>
                        </ul>
                    </div>
                </div>
            </div>
            <div class="col">
                <div class="card mb-4 shadow-sm">
                    <div class="card-header">
                        <h4 class="my-0 fw-normal">Protocol Optimization</h4>
                    </div>
                    <div class="card-body">
                        <p>Custom pluggable serial protocol filters enable efficient TCP packaging. Configurable packet caching and transmit timeouts optimize for either low latency or efficient bulk data transfers.</p>
                        <ul class="list-unstyled">
                            <li>• Ring buffer packet caching</li>
                            <li>• Adjustable transmit timeouts</li>
                            <li>• Pluggable filter system</li>
                        </ul>
                    </div>
                </div>
            </div>
        </div>

        <div class="row">
            <div class="col-md-6">
                <div class="card mb-4">
                    <div class="card-header">
                        <h5 class="my-0">Target Applications</h5>
                    </div>
                    <div class="card-body">
                        <ul>
                            <li><strong>Legacy Equipment Integration:</strong> Connect PLCs, sensors, and measurement instruments to SCADA systems</li>
                            <li><strong>Infrastructure Problem Solving:</strong> Overcome noise and cable length limitations in industrial environments</li>
                            <li><strong>Remote Device Access:</strong> Enable distant connections to serial devices through network infrastructure</li>
                        </ul>
                    </div>
                </div>
            </div>
            <div class="col-md-6">
                <div class="card mb-4">
                    <div class="card-header">
                        <h5 class="my-0">Technical Specifications</h5>
                    </div>
                    <div class="card-body">
                        <ul>
                            <li><strong>Hardware:</strong> RPI RP2350 microcontroller with 10Mbit Ethernet</li>
                            <li><strong>Interfaces:</strong> Up to 4 independent UART channels</li>
                            <li><strong>Performance:</strong> 500kBaud throughput, <5ms latency</li>
                            <li><strong>License:</strong> GPL - fully open source</li>
                            <li><strong>Compliance:</strong> CE certification, Western-only supply chain</li>
                        </ul>
                    </div>
                </div>
            </div>
        </div>
    </main>
</div>
