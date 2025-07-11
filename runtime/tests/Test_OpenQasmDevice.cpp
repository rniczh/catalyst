// Copyright 2023-2025 Xanadu Quantum Technologies Inc.

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <pybind11/embed.h>

#include "ExecutionContext.hpp"
#include "MemRefUtils.hpp"
#include "OpenQasmBuilder.hpp"
#include "RuntimeCAPI.h"

#include "OpenQasmDevice.hpp"
#include "OpenQasmRunner.hpp"

using namespace Catch::Matchers;
using namespace Catalyst::Runtime::Device;

using BType = OpenQasm::BuilderType;

TEST_CASE("Test OpenQasmRunner base class", "[openqasm]")
{
    // check the coverage support
    OpenQasm::OpenQasmRunner runner{};
    REQUIRE_THROWS_WITH(runner.runCircuit("", "", 0),
                        ContainsSubstring("[Function:runCircuit] Error in Catalyst Runtime: "
                                          "Not implemented method"));

    REQUIRE_THROWS_WITH(runner.Probs("", "", 0, 0),
                        ContainsSubstring("[Function:Probs] Error in Catalyst Runtime: "
                                          "Not implemented method"));

    REQUIRE_THROWS_WITH(runner.Sample("", "", 0, 0),
                        ContainsSubstring("[Function:Sample] Error in Catalyst Runtime: "
                                          "Not implemented method"));

    REQUIRE_THROWS_WITH(runner.Expval("", "", 0),
                        ContainsSubstring("[Function:Expval] Error in Catalyst Runtime: "
                                          "Not implemented method"));

    REQUIRE_THROWS_WITH(runner.Var("", "", 0),
                        ContainsSubstring("[Function:Var] Error in Catalyst Runtime: "
                                          "Not implemented method"));

    REQUIRE_THROWS_WITH(runner.State("", "", 0, 0),
                        ContainsSubstring("[Function:State] Error in Catalyst Runtime: "
                                          "Not implemented method"));

    REQUIRE_THROWS_WITH(runner.Gradient("", "", 0, 0),
                        ContainsSubstring("[Function:Gradient] Error in Catalyst Runtime: "
                                          "Not implemented method"));
}

TEST_CASE("Test BraketRunner", "[openqasm]")
{
    OpenQasm::BraketBuilder builder{};

    builder.Register(OpenQasm::RegisterType::Qubit, "q", 2);

    builder.Gate("RX", {0.5}, {}, {0}, false);
    builder.Gate("Hadamard", {}, {}, {1}, false);
    builder.Gate("CNOT", {}, {}, {0, 1}, false);
    builder.Gate("PauliY", {}, {}, {0}, false);

    auto &&circuit = builder.toOpenQasm();

    // Initializing the Python interpreter is required to run the circuit.
    // We use pybind11 for this since nanobind has no intention to support embedding a Python
    // interpreter in C++.
    if (!Py_IsInitialized()) {
        pybind11::initialize_interpreter();
    }

    OpenQasm::BraketRunner runner{};

    SECTION("Test BraketRunner::runCircuit()")
    {
        auto &&results = runner.runCircuit(circuit, "default", 100);
        CHECK(results.find("GateModelQuantumTaskResult") != std::string::npos);
    }

    SECTION("Test BraketRunner::Probs()")
    {
        auto &&probs = runner.Probs(circuit, "default", 100, 2);
        CHECK(probs.size() == 4); // For a 2-qubit system
        CHECK((probs[0] >= 0.0 && probs[0] <= 1.0));
        CHECK((probs[1] >= 0.0 && probs[1] <= 1.0));
        CHECK((probs[2] >= 0.0 && probs[2] <= 1.0));
        CHECK((probs[3] >= 0.0 && probs[3] <= 1.0));
    }

    SECTION("Test BraketRunner::Sample()")
    {
        auto &&samples = runner.Sample(circuit, "default", 100, 2);
        CHECK(samples.size() == 200); // Expecting 100 * 2 = 200 samples
        for (const auto &sample : samples) {
            REQUIRE(
                (sample == 0 || sample == 1)); // Each sample should be 0 or 1 for a 2-qubit state
        }
    }
}

TEST_CASE("Test BraketRunner Expval and Var", "[openqasm]")
{
    OpenQasm::BraketBuilder builder{};

    builder.Register(OpenQasm::RegisterType::Qubit, "q", 2);

    builder.Gate("RX", {0.5}, {}, {0}, false);
    builder.Gate("Hadamard", {}, {}, {1}, false);
    builder.Gate("CNOT", {}, {}, {0, 1}, false);

    // Initializing the Python interpreter is required to run the circuit.
    // We use pybind11 for this since nanobind has no intention to support embedding a Python
    // interpreter in C++.
    if (!Py_IsInitialized()) {
        pybind11::initialize_interpreter();
    }

    OpenQasm::BraketRunner runner{};

    SECTION("Test BraketRunner::Expval()")
    {
        // Compute expectation value of PauliY operator on qubit 0
        auto &&circuit_expval =
            builder.toOpenQasmWithCustomInstructions("#pragma braket result expectation y(q[0])");

        auto &&expval = runner.Expval(circuit_expval, "default", 100);
        CHECK((expval >= -1.0 && expval <= 1.0));
    }

    SECTION("Test BraketRunner::Expval() with no measurement process")
    {
        // Cannot compute expectation value if no measurement process is defined
        auto &&circuit_expval = builder.toOpenQasmWithCustomInstructions("");

        REQUIRE_THROWS_WITH(runner.Expval(circuit_expval, "default", 100),
                            ContainsSubstring("Unable to compute expectation value"));
    }

    SECTION("Test BraketRunner::Var()")
    {
        // Compute variance of PauliY operator on qubit 0
        auto &&circuit_var =
            builder.toOpenQasmWithCustomInstructions("#pragma braket result variance y(q[0])");

        auto &&var = runner.Var(circuit_var, "default", 100);
        CHECK((var >= 0.0 && var <= 1.0));
    }

    SECTION("Test BraketRunner::Var() with no measurement process")
    {
        // Cannot compute variance if no measurement process is defined
        auto &&circuit_var = builder.toOpenQasmWithCustomInstructions("");

        REQUIRE_THROWS_WITH(runner.Var(circuit_var, "default", 100),
                            ContainsSubstring("Unable to compute variance"));
    }
}

TEST_CASE("Test the OpenQasmDevice constructor", "[openqasm]")
{
    SECTION("Common")
    {
        auto device = OpenQasmDevice("{}");
        CHECK(device.GetNumQubits() == 0);

        REQUIRE_THROWS_WITH(device.Circuit(),
                            ContainsSubstring("[Function:toOpenQasm] Error in Catalyst Runtime: "
                                              "Invalid number of quantum register"));
    }

    SECTION("Braket SV1")
    {
        auto device = OpenQasmDevice("{device_type : braket.local.qubit, backend : default}");
        CHECK(device.GetNumQubits() == 0);

        REQUIRE_THROWS_WITH(device.Circuit(),
                            ContainsSubstring("[Function:toOpenQasm] Error in Catalyst Runtime: "
                                              "Invalid number of quantum register"));
    }
}

TEST_CASE("Test qubits allocation OpenQasmDevice", "[openqasm]")
{
    std::unique_ptr<OpenQasmDevice> device = std::make_unique<OpenQasmDevice>("{}");

    constexpr size_t n = 3;
    device->AllocateQubits(1);
    CHECK(device->GetNumQubits() == 1);

    REQUIRE_THROWS_WITH(
        device->AllocateQubits(n - 1),
        ContainsSubstring("[Function:AllocateQubits] Error in Catalyst Runtime: Partial qubits "
                          "allocation is not supported by OpenQasmDevice"));
}

TEST_CASE("Test the OpenQasmDevice setBasisState", "[openqasm]")
{
    std::unique_ptr<OpenQasmDevice> device = std::make_unique<OpenQasmDevice>("{}");

    constexpr size_t n = 2;
    device->AllocateQubits(n);

    std::vector<int8_t> state{1};
    DataView<int8_t, 1> view(state);
    std::vector<QubitIdType> wires{0};
    REQUIRE_THROWS_WITH(device->SetBasisState(view, wires),
                        ContainsSubstring("unsupported by device"));
}

TEST_CASE("Test the OpenQasmDevice setState", "[openqasm]")
{
    std::unique_ptr<OpenQasmDevice> device = std::make_unique<OpenQasmDevice>("{}");

    constexpr size_t n = 2;
    device->AllocateQubits(n);

    std::vector<std::complex<double>> state{{1.0, 0.0}};
    DataView<std::complex<double>, 1> view(state);
    std::vector<QubitIdType> wires{0};
    REQUIRE_THROWS_WITH(device->SetState(view, wires), ContainsSubstring("unsupported by device"));
}

TEST_CASE("Test the bell pair circuit with BuilderType::Common", "[openqasm]")
{
    std::unique_ptr<OpenQasmDevice> device = std::make_unique<OpenQasmDevice>("{}");

    constexpr size_t n = 2;
    auto wires = device->AllocateQubits(n);

    device->NamedOperation("Hadamard", {}, {wires[0]}, false);
    device->NamedOperation("CNOT", {}, {wires[0], wires[1]}, false);

    device->Measure(wires[1]);

    std::string toqasm = "OPENQASM 3.0;\n"
                         "qubit[2] qubits;\n"
                         "bit[2] bits;\n"
                         "h qubits[0];\n"
                         "cnot qubits[0], qubits[1];\n"
                         "bits[1] = measure qubits[1];\n"
                         "reset qubits;\n";

    CHECK(device->Circuit() == toqasm);
}

TEST_CASE("Test measurement processes, the bell pair circuit with BuilderType::Braket",
          "[openqasm]")
{
    constexpr size_t shots{1000};
    std::unique_ptr<OpenQasmDevice> device =
        std::make_unique<OpenQasmDevice>("{device_type : braket.local.qubit, backend : default}");
    device->SetDeviceShots(shots);

    constexpr size_t n{2};
    constexpr size_t size{1UL << n};
    auto wires = device->AllocateQubits(n);

    device->NamedOperation("Hadamard", {}, {wires[0]}, false);
    device->NamedOperation("CNOT", {}, {wires[0], wires[1]}, false);

    std::string toqasm = "OPENQASM 3.0;\n"
                         "qubit[2] qubits;\n"
                         "bit[2] bits;\n"
                         "h qubits[0];\n"
                         "cnot qubits[0], qubits[1];\n"
                         "bits = measure qubits;\n";

    CHECK(device->Circuit() == toqasm);

    SECTION("Probs")
    {
        std::vector<double> probs(size);
        DataView<double, 1> view(probs);
        device->Probs(view);

        CHECK(probs[1] == probs[2]);
        CHECK(probs[0] + probs[3] == Catch::Approx(1.f).margin(1e-5));
    }

    SECTION("PartialProbs")
    {
        std::vector<double> probs(size);
        DataView<double, 1> view(probs);
        device->PartialProbs(view, std::vector<QubitIdType>{0, 1});

        CHECK(probs[0] + probs[3] == Catch::Approx(1.f).margin(1e-5));
    }

    SECTION("Samples")
    {
        std::vector<double> samples(shots * n);
        MemRefT<double, 2> buffer{samples.data(), samples.data(), 0, {shots, n}, {1, 1}};
        DataView<double, 2> view(buffer.data_aligned, buffer.offset, buffer.sizes, buffer.strides);
        device->Sample(view);

        for (size_t i = 0; i < shots * n; i++) {
            CHECK((samples[i] == 0.f || samples[i] == 1.f));
        }
    }

    SECTION("PartialSamples")
    {
        std::vector<double> samples(shots);
        MemRefT<double, 2> buffer{samples.data(), samples.data(), 0, {shots, 1}, {1, 1}};
        DataView<double, 2> view(buffer.data_aligned, buffer.offset, buffer.sizes, buffer.strides);
        device->PartialSample(view, std::vector<QubitIdType>{0});

        for (size_t i = 0; i < shots; i++) {
            CHECK((samples[i] == 0.f || samples[i] == 1.f));
        }
    }

    SECTION("Counts")
    {
        std::vector<double> eigvals(size);
        std::vector<int64_t> counts(size);
        DataView<double, 1> eview(eigvals);
        DataView<int64_t, 1> cview(counts);
        device->Counts(eview, cview);

        size_t sum = 0;
        for (size_t i = 0; i < size; i++) {
            CHECK(eigvals[i] == static_cast<double>(i));
            sum += counts[i];
        }
        CHECK(sum == shots);
    }

    SECTION("PartialCounts")
    {
        size_t size = (1UL << 1);
        std::vector<double> eigvals(size);
        std::vector<int64_t> counts(size);
        DataView<double, 1> eview(eigvals);
        DataView<int64_t, 1> cview(counts);
        device->PartialCounts(eview, cview, std::vector<QubitIdType>{1});

        size_t sum = 0;
        for (size_t i = 0; i < size; i++) {
            CHECK(eigvals[i] == static_cast<double>(i));
            sum += counts[i];
        }
        CHECK(sum == shots);
    }

    SECTION("Expval(h(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        auto obs = device->Observable(ObsId::Hadamard, {}, std::vector<QubitIdType>{1});
        auto expval = device->Expval(obs);
        CHECK(expval == Catch::Approx(0.0).margin(1e-5));
    }

    SECTION("Expval(x(0) @ h(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        auto obs_x = device->Observable(ObsId::PauliX, {}, std::vector<QubitIdType>{0});
        auto obs_h = device->Observable(ObsId::Hadamard, {}, std::vector<QubitIdType>{1});
        auto obs = device->TensorObservable({obs_x, obs_h});
        auto expval = device->Expval(obs);
        CHECK(expval == Catch::Approx(0.7071067812).margin(1e-5));
    }

    SECTION("Var(h(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        auto obs = device->Observable(ObsId::Hadamard, {}, std::vector<QubitIdType>{1});
        auto expval = device->Var(obs);
        CHECK(expval == Catch::Approx(1.0).margin(1e-5));
    }

    SECTION("Var(x(0) @ h(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        auto obs_x = device->Observable(ObsId::PauliX, {}, std::vector<QubitIdType>{0});
        auto obs_h = device->Observable(ObsId::Hadamard, {}, std::vector<QubitIdType>{1});
        auto obs = device->TensorObservable({obs_x, obs_h});
        auto expval = device->Var(obs);
        CHECK(expval == Catch::Approx(0.5).margin(1e-5));
    }
}

TEST_CASE("Test measurement processes, a simple circuit with BuilderType::Braket", "[openqasm]")
{
    constexpr size_t shots{1000};
    std::unique_ptr<OpenQasmDevice> device =
        std::make_unique<OpenQasmDevice>("{device_type : braket.local.qubit, backend : default}");
    device->SetDeviceShots(shots);

    constexpr size_t n{5};
    constexpr size_t size{1UL << n};
    auto wires = device->AllocateQubits(n);

    device->NamedOperation("PauliX", {}, {wires[0]}, false);
    device->NamedOperation("PauliY", {}, {wires[1]}, false);
    device->NamedOperation("PauliZ", {}, {wires[2]}, false);
    device->NamedOperation("RX", {0.6}, {wires[4]}, false);
    device->NamedOperation("CNOT", {}, {wires[0], wires[3]}, false);
    device->NamedOperation("Toffoli", {}, {wires[0], wires[3], wires[4]}, false);

    std::string toqasm = "OPENQASM 3.0;\n"
                         "qubit[5] qubits;\n"
                         "bit[5] bits;\n"
                         "x qubits[0];\n"
                         "y qubits[1];\n"
                         "z qubits[2];\n"
                         "rx(0.6) qubits[4];\n"
                         "cnot qubits[0], qubits[3];\n"
                         "ccnot qubits[0], qubits[3], qubits[4];\n"
                         "bits = measure qubits;\n";

    CHECK(device->Circuit() == toqasm);

    SECTION("Probs")
    {
        std::vector<double> probs(size);
        DataView<double, 1> view(probs);
        device->Probs(view);

        CHECK(probs[27] + probs[26] == Catch::Approx(1.f).margin(1e-5));
    }

    SECTION("PartialProbs")
    {
        std::vector<double> probs(size);
        DataView<double, 1> view(probs);
        device->PartialProbs(view, std::vector<QubitIdType>{0, 1, 2, 3, 4});

        CHECK(probs[27] + probs[26] == Catch::Approx(1.f).margin(1e-5));
    }

    SECTION("Samples")
    {
        std::vector<double> samples(shots * n);
        MemRefT<double, 2> buffer{samples.data(), samples.data(), 0, {shots, n}, {1, 1}};
        DataView<double, 2> view(buffer.data_aligned, buffer.offset, buffer.sizes, buffer.strides);
        device->Sample(view);

        for (size_t i = 0; i < shots * n; i++) {
            CHECK((samples[i] == 0.f || samples[i] == 1.f));
        }
    }

    SECTION("PartialSamples")
    {
        std::vector<double> samples(shots);
        MemRefT<double, 2> buffer{samples.data(), samples.data(), 0, {shots, 1}, {1, 1}};
        DataView<double, 2> view(buffer.data_aligned, buffer.offset, buffer.sizes, buffer.strides);
        device->PartialSample(view, std::vector<QubitIdType>{0});

        for (size_t i = 0; i < shots; i++) {
            CHECK((samples[i] == 0.f || samples[i] == 1.f));
        }
    }

    SECTION("Counts")
    {
        std::vector<double> eigvals(size);
        std::vector<int64_t> counts(size);
        DataView<double, 1> eview(eigvals);
        DataView<int64_t, 1> cview(counts);
        device->Counts(eview, cview);

        size_t sum = 0;
        for (size_t i = 0; i < size; i++) {
            CHECK(eigvals[i] == static_cast<double>(i));
            sum += counts[i];
        }
        CHECK(sum == shots);
    }

    SECTION("PartialCounts")
    {
        size_t size = (1UL << 1);
        std::vector<double> eigvals(size);
        std::vector<int64_t> counts(size);
        DataView<double, 1> eview(eigvals);
        DataView<int64_t, 1> cview(counts);
        device->PartialCounts(eview, cview, std::vector<QubitIdType>{1});

        size_t sum = 0;
        for (size_t i = 0; i < size; i++) {
            CHECK(eigvals[i] == static_cast<double>(i));
            sum += counts[i];
        }
        CHECK(sum == shots);
    }

    SECTION("Expval(h(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        auto obs = device->Observable(ObsId::Hadamard, {}, std::vector<QubitIdType>{1});
        auto expval = device->Expval(obs);
        CHECK(expval == Catch::Approx(-0.7071067812).margin(1e-5));
    }

    SECTION("Expval(hermitian(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        std::vector<std::complex<double>> matrix{
            {0, 0},
            {0, -1},
            {0, 1},
            {0, 0},
        };
        auto obs = device->Observable(ObsId::Hermitian, matrix, std::vector<QubitIdType>{1});
        auto expval = device->Expval(obs);
        CHECK(expval == Catch::Approx(0).margin(1e-5));
    }

    SECTION("Expval(x(0) @ h(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        auto obs_z = device->Observable(ObsId::PauliZ, {}, std::vector<QubitIdType>{0});
        auto obs_h = device->Observable(ObsId::Hadamard, {}, std::vector<QubitIdType>{1});
        auto tp = device->TensorObservable({obs_z, obs_h});
        auto expval = device->Expval(tp);
        CHECK(expval == Catch::Approx(0.7071067812).margin(1e-5));

        auto obs = device->HamiltonianObservable({0.2}, {tp});
        REQUIRE_THROWS_WITH(device->Expval(obs),
                            ContainsSubstring("Unsupported observable: QasmHamiltonianObs"));
    }

    SECTION("Var(h(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        auto obs = device->Observable(ObsId::Hadamard, {}, std::vector<QubitIdType>{1});
        auto var = device->Var(obs);
        CHECK(var == Catch::Approx(0.5).margin(1e-5));
    }

    SECTION("Var(hermitian(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        std::vector<std::complex<double>> matrix{
            {0, 0},
            {0, -1},
            {0, 1},
            {0, 0},
        };
        auto obs = device->Observable(ObsId::Hermitian, matrix, std::vector<QubitIdType>{1});
        auto var = device->Var(obs);
        CHECK(var == Catch::Approx(1).margin(1e-5));
    }

    SECTION("Var(x(0) @ h(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        auto obs_z = device->Observable(ObsId::PauliZ, {}, std::vector<QubitIdType>{0});
        auto obs_h = device->Observable(ObsId::Hadamard, {}, std::vector<QubitIdType>{1});
        auto tp = device->TensorObservable({obs_z, obs_h});
        auto var = device->Var(tp);
        CHECK(var == Catch::Approx(0.5).margin(1e-5));

        auto obs = device->HamiltonianObservable({0.2}, {tp});
        REQUIRE_THROWS_WITH(device->Var(obs),
                            ContainsSubstring("Unsupported observable: QasmHamiltonianObs"));
    }
}

TEST_CASE("Test MatrixOperation with BuilderType::Braket", "[openqasm]")
{
    std::unique_ptr<OpenQasmDevice> device =
        std::make_unique<OpenQasmDevice>("{device_type : braket.local.qubit, backend : default}");
    device->SetDeviceShots(1000);

    constexpr size_t n{2};
    constexpr size_t size{1UL << n};
    auto wires = device->AllocateQubits(n);

    device->NamedOperation("PauliX", {}, {wires[0]}, false);
    device->NamedOperation("PauliY", {}, {wires[1]}, false);
    std::vector<std::complex<double>> matrix{
        {0, 0},
        {0, -1},
        {0, 1},
        {0, 0},
    };
    device->MatrixOperation(matrix, {wires[0]}, false);

    std::string toqasm = "OPENQASM 3.0;\n"
                         "qubit[2] qubits;\n"
                         "bit[2] bits;\n"
                         "x qubits[0];\n"
                         "y qubits[1];\n"
                         "#pragma braket unitary([[0, 0-1im], [0+1im, 0]]) qubits[0]\n"
                         "bits = measure qubits;\n";

    CHECK(device->Circuit() == toqasm);

    SECTION("Probs")
    {
        std::vector<double> probs(size);
        DataView<double, 1> view(probs);
        device->Probs(view);
        CHECK(probs[1] == Catch::Approx(1.f).margin(1e-5));
    }

    SECTION("Expval(h(1))")
    {
        device->SetDeviceShots(0); // to get deterministic results
        auto obs = device->Observable(ObsId::Hadamard, {}, std::vector<QubitIdType>{1});
        auto expval = device->Expval(obs);
        CHECK(expval == Catch::Approx(-0.7071067812).margin(1e-5));
    }
}

TEST_CASE("Test PSWAP and ISWAP with BuilderType::Braket", "[openqasm]")
{
    std::unique_ptr<OpenQasmDevice> device =
        std::make_unique<OpenQasmDevice>("{device_type : braket.local.qubit, backend : default}");
    device->SetDeviceShots(1000);

    constexpr size_t n{2};
    auto wires = device->AllocateQubits(n);

    device->NamedOperation("Hadamard", {}, {wires[0]}, false);
    device->NamedOperation("ISWAP", {}, {wires[0], wires[1]}, false);
    device->NamedOperation("PSWAP", {0}, {wires[0], wires[1]}, false);

    auto obs = device->Observable(ObsId::PauliZ, {}, std::vector<QubitIdType>{1});
    auto expval = device->Expval(obs);
    CHECK(expval == Catch::Approx(1).margin(1e-5));
}

TEST_CASE("Test MatrixOperation with OpenQasmDevice and BuilderType::Common", "[openqasm]")
{
    auto device = OpenQasmDevice("{}");
    auto wires = device.AllocateQubits(2);
    std::vector<std::complex<double>> matrix{
        {0, 0},
        {0, -1},
        {0, 1},
        {0, 0},
    };

    REQUIRE_THROWS_WITH(device.MatrixOperation(matrix, {wires[0]}, false),
                        ContainsSubstring("Unsupported functionality"));
}

TEST_CASE("Test __catalyst__rt__device_init registering the OpenQasm device", "[CoreQIS]")
{
    __catalyst__rt__initialize(nullptr);

    char device_aws[30] = "braket.aws.qubit";

#if __has_include("OpenQasmDevice.hpp")
    __catalyst__rt__device_init((int8_t *)device_aws, nullptr, nullptr, 0, false);
#else
    REQUIRE_THROWS_WITH(
        __catalyst__rt__device_init((int8_t *)device_aws, nullptr, nullptr, 0, false),
        ContainsSubstring("cannot open shared object file"));
#endif

    __catalyst__rt__finalize();

    __catalyst__rt__initialize(nullptr);

    char device_local[30] = "braket.local.qubit";

#if __has_include("OpenQasmDevice.hpp")
    __catalyst__rt__device_init((int8_t *)device_local, nullptr, nullptr, 0, false);
#else
    REQUIRE_THROWS_WITH(
        __catalyst__rt__device_init((int8_t *)(int8_t *), nullptr, nullptr, 0, false),
        ContainsSubstring("cannot open shared object file"));
#endif

    __catalyst__rt__finalize();
}
