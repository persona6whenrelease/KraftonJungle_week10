# 렌더링 자원 관리 및 명령 파이프라인 분석

## 1. 자원 바인딩의 단위: FDrawCommand
드로우 콜 하나에 필요한 모든 정보가 `FDrawCommand` 구조체에 캡슐화되어 있습니다.
* **Geometry:** VB/IB 및 드로우 범위 (`FDrawCommandBuffer`).
* **Bindings:** 재질별 셰이더 상수 버퍼(CB b2, b3) 및 텍스처 리소스(SRV t0~t7).
* **RenderState:** DepthStencil, Blend, Rasterizer 상태.
* **SortKey:** 패스, 셰이더, 메쉬, 텍스처 순으로 생성된 64비트 키.

## 2. 효율 극대화: FDrawCommandList::Sort()
제출 전, 모든 커맨드는 `SortKey`를 기준으로 정렬됩니다.
* **자동 최적화:** 동일한 셰이더, 동일한 메쉬, 동일한 텍스처를 사용하는 드로우 콜들이 연속적으로 배치됩니다. 이는 `FStateCache`의 효율을 극대화하는 핵심 단계입니다.

## 3. 중복 상태 전환 방지: FStateCache
`FDrawCommandList::SubmitCommand` 함수에서 실제 GPU 제출이 일어날 때, `FStateCache`를 참조하여 변경된 자원만 바인딩합니다.

```cpp
// DrawCommandList.cpp 내 로직 예시
if (bForce || Cmd.Shader != Cache.Shader) {
    Cmd.Shader->Bind(Ctx); // 셰이더가 다를 때만 바인딩
    Cache.Shader = Cmd.Shader;
}

for (int i = 0; i < MaxSRV; i++) {
    if (bForce || Cmd.Bindings.SRVs[i] != Cache.Bindings.SRVs[i]) {
        Ctx->PSSetShaderResources(i, 1, &Cmd.Bindings.SRVs[i]); // 변경된 텍스처만 갱신
        Cache.Bindings.SRVs[i] = Cmd.Bindings.SRVs[i];
    }
}
```

* **효과:** 동일 자원을 사용하는 연속된 드로우 콜에서 불필요한 D3D11 API 호출(CPU Overhead)을 자동으로 제거합니다.

## 4. 패스 단위의 자원 오케스트레이션: FRenderPassBase
각 렌더패스는 `BeginPass`와 `EndPass`를 통해 패스 수준의 자원 생명주기를 관리합니다.
* **BeginPass:** 해당 패스에서 사용할 Render Target(RTV)과 Depth Stencil(DSV)을 바인딩합니다. (예: OpaquePass에서의 MRT 설정)
* **EndPass:** 패스가 끝나면 사용한 RT를 해제하거나, 다음 패스에서 읽을 수 있도록 셰이더 리소스(SRV)로 바인딩합니다.
* **자동 전환:** `FRenderPassPipeline::Execute`가 등록된 모든 패스를 순회하며 이 과정을 자동으로 수행합니다.

## 5. 시스템 자원 관리: FSystemResources
프레임 버퍼, 광원 정보 등 엔진 전역에서 사용되는 공통 자원들은 `FSystemResources`에서 관리됩니다.
* **데이터 전송:** `UpdateFrameBuffer`, `UpdateLightBuffer` 등을 통해 매 프레임 필요한 데이터를 GPU로 자동 전송합니다.
* **상태 유지:** `BindSystemSamplers` 등을 통해 공통 샘플러 상태를 영구적으로 유지합니다.

---

### 요약: 자원 관리 흐름
1. **Collector:** 각 액터가 본인의 자원(메쉬, 재질)을 `FDrawCommand`에 담아 리스트에 추가합니다.
2. **Sort:** 리스트를 정렬하여 자원 변경 횟수를 최소화할 수 있는 순서로 재배치합니다.
3. **Pipeline Execute:** 각 패스의 `BeginPass`에서 출력 타겟(RT/DSV)을 설정합니다.
4. **Submit:** `FStateCache`가 이전 드로우 콜과 현재 드로우 콜을 비교하여, 실제로 달라진 자원만 GPU에 바인딩하고 드로우 명령을 내립니다.
5. **Cleanup:** 프레임 종료 시 `FStateCache::Cleanup`을 통해 바인딩된 리소스들을 정리하여 자원 Hazard를 방지합니다.

**결론:** 새로운 메시나 재질 추가 시 별도의 바인딩 로직 없이 `FDrawCommand` 자원 할당만으로 시스템이 최적의 순서로 자동 제출을 수행합니다.