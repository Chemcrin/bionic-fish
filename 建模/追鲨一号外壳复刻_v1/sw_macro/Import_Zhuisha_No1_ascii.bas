Attribute VB_Name = "Import_Zhuisha_No1_ascii"
Option Explicit

' SolidWorks 2025 macro for the Zhuisha No.1 exterior-shell package.
' Imported STL meshes are saved as native SLDPRT files for review/assembly.

Private Const SW_DOC_PART As Long = 1
Private Const SW_OPEN_SILENT As Long = 1
Private Const SW_SAVE_CURRENT As Long = 0
Private Const SW_SAVE_SILENT As Long = 1

Public Sub Main()
    Dim swApp As Object, root As String, outDir As String
    Dim errCode As Long, warnCode As Long, fullPart As Object
    On Error GoTo Fail

    Set swApp = Application.SldWorks
    root = ThisMacroPath(swApp)
    outDir = root & "\..\out"
    If Dir(outDir, vbDirectory) = "" Then MkDir outDir

    Set fullPart = ImportPart(swApp, outDir & "\zhuisha_no1_v1.stl", _
                              outDir & "\Whale.sldprt", errCode, warnCode)
    If fullPart Is Nothing Then Err.Raise vbObjectError + 100, , "Full STL import failed"
    swApp.CloseDoc fullPart.GetTitle

    ImportPart swApp, outDir & "\head_shell_mesh.stl", outDir & "\Head.SLDPRT", errCode, warnCode
    ImportPart swApp, outDir & "\body_shell_mesh.stl", outDir & "\Body.SLDPRT", errCode, warnCode
    ImportPart swApp, outDir & "\tail_shell_mesh.stl", outDir & "\Tail.SLDPRT", errCode, warnCode

    ExportAssembly swApp, outDir
    ValidateOutputs outDir
    MsgBox "Zhuisha No.1 exterior import completed. Output folder: " & outDir, vbInformation
    Exit Sub
Fail:
    MsgBox "Zhuisha No.1 macro failed: " & Err.Description, vbCritical
End Sub

Private Function ImportPart(ByVal swApp As Object, ByVal stlPath As String, _
                            ByVal savePath As String, ByRef errCode As Long, _
                            ByRef warnCode As Long) As Object
    Dim model As Object
    If Dir(stlPath) = "" Then Err.Raise vbObjectError + 101, , "Missing STL: " & stlPath
    Set model = swApp.OpenDoc6(stlPath, SW_DOC_PART, SW_OPEN_SILENT, "", errCode, warnCode)
    If model Is Nothing Then Exit Function
    model.Extension.SelectByID2 "", "SOLIDBODY", 0, 0, 0, False, 0, Nothing, 0
    model.SaveAs3 savePath, SW_SAVE_CURRENT, SW_SAVE_SILENT
    Set ImportPart = model
End Function

Private Sub ExportAssembly(ByVal swApp As Object, ByVal outDir As String)
    Dim asm As Object, template As String
    template = swApp.GetUserPreferenceStringValue(1)
    If template = "" Or Dir(template) = "" Then Exit Sub
    Set asm = swApp.NewDocument(template, 0, 0, 0)
    If asm Is Nothing Then Exit Sub
    asm.AddComponent5 outDir & "\Head.SLDPRT", 0, "", False, "", 0, 0, 0
    asm.AddComponent5 outDir & "\Body.SLDPRT", 0, "", False, "", 0.18, 0, 0
    asm.AddComponent5 outDir & "\Tail.SLDPRT", 0, "", False, "", 0.52, 0, 0
    asm.SaveAs3 outDir & "\Zhuisha_No1_Assembly.SLDASM", SW_SAVE_CURRENT, SW_SAVE_SILENT
    asm.Extension.SelectByID2 "", "COMPONENT", 0, 0, 0, True, 0, Nothing, 0
    asm.SaveAs3 outDir & "\Zhuisha_No1_Assembly.STEP", SW_SAVE_CURRENT, SW_SAVE_SILENT
    swApp.CloseDoc asm.GetTitle
End Sub

Private Sub ValidateOutputs(ByVal outDir As String)
    Dim names, n, p As String, missing As String
    names = Array("Whale.sldprt", "Head.SLDPRT", "Body.SLDPRT", "Tail.SLDPRT", _
                  "Zhuisha_No1_Assembly.SLDASM", "Zhuisha_No1_Assembly.STEP")
    For Each n In names
        p = outDir & "\" & CStr(n)
        If Dir(p) = "" Then missing = missing & vbCrLf & p
    Next n
    If missing <> "" Then
        MsgBox "Validate: missing output files:" & missing, vbExclamation
    Else
        Debug.Print "Validate: all Zhuisha No.1 outputs exist."
    End If
End Sub

Private Function ThisMacroPath(ByVal swApp As Object) As String
    Dim p As String
    p = swApp.GetCurrentMacroPathName
    If InStrRev(p, "\") > 0 Then
        ThisMacroPath = Left$(p, InStrRev(p, "\") - 1)
    Else
        ThisMacroPath = CurDir$
    End If
End Function
