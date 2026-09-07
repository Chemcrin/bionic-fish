**Role**: 你是一位精通 Python 几何建模、SolidWorks 2025 API 和 FDM 增材制造设计的机器人研发工程师。

**Task**: 请编写一段 Python 自动化脚本，用于驱动本机安装的 SolidWorks，生成仿生虎鲸三段式低多边形外壳。

**Environment**: Windows 11，SolidWorks 2025 中文版，PETG FDM 3D 打印工艺。

**Deliverables**:

1. 一个名为 `Whale.sldprt` 的文件。
    
2. 前段 (`Head.SLDPRT`)、中段 (`Body.SLDPRT`)、后段 (`Tail.SLDPRT`) 的独立零件文件。
    
3. 至少 1 个 `STEP` 格式的装配体总装文件。
    

**Design Specs**:

- 尺寸：总长 420mm；中段最大直径 180mm。
    
- 分段：X轴 0-120mm (头部)，120-300mm (躯干)，300-420mm (尾部)。
    
- 形态：低多边形，采用三角形切面；外形需体现虎鲸的特征背鳍、胸鳍及流线型吻部。
    
- 内部：每段均为中空结构，壁厚 1.5mm。在每段的结合处设计有精度公差为 ±0.1mm 的定位止口和装配孔。
    

**Technical Constraints (避坑指南)**:

1. 单位换算：SolidWorks API 底层单位是米，必须创建 `mm2m` 函数进行转化 [](https://www.snm0516.aisee.tv/video/BV1aiAwz7Ecd/?spm_id_from=333.788.recommend_more_video.4)。
    
2. 基准面选择：必须优先按中文名选择 `前视基准面`、`右视基准面` 等 [](https://www.snm0516.aisee.tv/video/BV1aiAwz7Ecd/?spm_id_from=333.788.recommend_more_video.4)。
    
3. 草绘规范：不使用复杂几何约束，建议用绝对坐标直接绘制多边形轮廓；使用 `SketchManager.AddToDB = True` 提升稳定性 [](https://www.snm0516.aisee.tv/video/BV1aiAwz7Ecd/?spm_id_from=333.788.recommend_more_video.4)。
    
4. 导出逻辑：完成所有特征后，必须通过 `Extension.SelectByID2` 选中模型，并调用 `SaveAs3` 导出 `.SLDPRT` 和 `.STEP` 文件 [](https://github.com/Sakikooooo/solidworks-auto-modeling#1)[](https://www.snm0516.aisee.tv/video/BV1aiAwz7Ecd/?spm_id_from=333.788.recommend_more_video.4)。
    

**Procedure**:

1. 首先在终端打印关键参数，并且尝试渲染最终成果的大致图样，等待用户确认。如果有不明白的地方及时提出问题
    
2. 使用循环和阵列生成折面骨架。
    
3. 保存文件到指定 ASCII 路径（如 `C:\temp`），避免中文路径报错。
    
4. 最后执行 `Validate` 输出，确认文件已真实生成。
5. 样式可参照以下他人制作的【追鲨一号】冲浪器外壳样式思考制作方案：BV1Qquz6XE8q![](assets/外壳建模prompt/file-20260825160459261.png)![](assets/外壳建模prompt/file-20260825160527805.png)