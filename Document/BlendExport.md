Blender FBX 내보내기 설정 체크리스트
Tab: Include
설정권장값이유Object TypesArmature + Mesh만 체크Lamp 등 불필요한 노드가 포함되면 Scene에 eSkeleton 아닌 노드가 섞임

Tab: Transform
설정권장값이유Scale1.0스케일이 다르면 M 행렬에 스케일이 실려 IBP 오염Apply Transform✅ 켜기이미 하고 계심 ✅Forward / Up Axis엔진 축 규격에 맞게코드가 ConvertScene()으로 Z-Up Left-Handed로 변환하므로, Blender 기본값(-Z Forward, Y Up)이면 ConvertScene이 자동 처리

Tab: Geometry
설정권장값이유SmoothingFace 또는 EdgeNormals Only면 GetPolygonVertexNormal() 실패 → Normal = zeroTriangulate Mesh✅ 켜기코드가 PolygonSize != 3을 경고 없이 skip함. 쿼드 메시면 폴리곤 대량 누락

Tab: Armature
설정권장값이유Primary Bone AxisY (기본값 유지)변경 시 본 로컬 회전이 틀어진 채로 TLM에 저장됨. ConvertScene()이 축 변환을 처리하므로 기본값 권장Secondary Bone AxisX (기본값 유지)위와 동일한 이유Add Leaf Bones❌ 끄기이미 끄고 계심 ✅Only Deform Bones✅ 켜기IK 타겟 등 컨트롤 본이 있으면 Cluster 없는 eSkeleton 노드로 export되어 본 인덱스 오염



현재 구조(body, eye가 Armature 자식)에서 추가 주의사항
body와 eye 각각에 대해 Object Mode → N패널에서:

Location : 0, 0, 0
Rotation : 0, 0, 0  
Scale    : 1, 1, 1