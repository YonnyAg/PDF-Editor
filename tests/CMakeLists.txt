qt_add_executable(main_window_tests MainWindowTest.cpp)
target_link_libraries(main_window_tests PRIVATE pdf_editor_ui Qt6::Test)
pdf_editor_enable_warnings(main_window_tests)

add_test(NAME ui.main_window COMMAND main_window_tests)
set_tests_properties(ui.main_window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30
)

qt_add_executable(pdf_document_tests PdfDocumentTest.cpp)
target_link_libraries(pdf_document_tests PRIVATE pdf_editor_core Qt6::Gui Qt6::Test Pdf4QtLibCore)
pdf_editor_enable_warnings(pdf_document_tests)
add_test(NAME core.pdf_document COMMAND pdf_document_tests)
set_tests_properties(core.pdf_document PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60
)

qt_add_executable(page_render_tests PageRenderTest.cpp)
target_link_libraries(page_render_tests PRIVATE pdf_editor_core Qt6::Test)
pdf_editor_enable_warnings(page_render_tests)
add_test(NAME core.page_render COMMAND page_render_tests)
set_tests_properties(core.page_render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(page_view_tests PageViewTest.cpp)
target_link_libraries(page_view_tests PRIVATE pdf_editor_ui Qt6::Test)
pdf_editor_enable_warnings(page_view_tests)
add_test(NAME ui.page_view COMMAND page_view_tests)
set_tests_properties(ui.page_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(session_store_tests SessionStoreTest.cpp)
target_link_libraries(session_store_tests PRIVATE pdf_editor_core Qt6::Test)
pdf_editor_enable_warnings(session_store_tests)
add_test(NAME session.store COMMAND session_store_tests)
set_tests_properties(session.store PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(workspace_window_tests WorkspaceWindowTest.cpp)
target_link_libraries(workspace_window_tests PRIVATE pdf_editor_ui Qt6::Test)
pdf_editor_enable_warnings(workspace_window_tests)
add_test(NAME ui.workspace COMMAND workspace_window_tests)
set_tests_properties(ui.workspace PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

# Keep QtTest diagnostics visible in CTest and CI on Windows.
set_target_properties(main_window_tests pdf_document_tests page_render_tests page_view_tests
    session_store_tests workspace_window_tests PROPERTIES WIN32_EXECUTABLE FALSE)

qt_add_executable(advanced_tools_tests AdvancedToolsTest.cpp)
target_link_libraries(advanced_tools_tests PRIVATE pdf_editor_core Qt6::Test)
pdf_editor_enable_warnings(advanced_tools_tests)
set_target_properties(advanced_tools_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME core.advanced_tools COMMAND advanced_tools_tests)
set_tests_properties(core.advanced_tools PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(advanced_window_tests AdvancedWindowTest.cpp)
target_link_libraries(advanced_window_tests PRIVATE pdf_editor_ui Qt6::Test)
pdf_editor_enable_warnings(advanced_window_tests)
set_target_properties(advanced_window_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME ui.advanced_tools COMMAND advanced_window_tests)
set_tests_properties(ui.advanced_tools PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(reader_safety_tests ReaderSafetyTest.cpp)
target_link_libraries(reader_safety_tests PRIVATE pdf_editor_core Qt6::Test)
pdf_editor_enable_warnings(reader_safety_tests)
set_target_properties(reader_safety_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME core.reader_safety COMMAND reader_safety_tests)
set_tests_properties(core.reader_safety PROPERTIES TIMEOUT 90)

qt_add_executable(security_ocr_tests SecurityOcrTest.cpp)
target_link_libraries(security_ocr_tests PRIVATE pdf_editor_core Qt6::Test)
pdf_editor_enable_warnings(security_ocr_tests)
set_target_properties(security_ocr_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME core.security_ocr COMMAND security_ocr_tests)
set_tests_properties(core.security_ocr PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

qt_add_executable(security_ocr_window_tests SecurityOcrWindowTest.cpp)
target_link_libraries(security_ocr_window_tests PRIVATE pdf_editor_ui Qt6::Test)
pdf_editor_enable_warnings(security_ocr_window_tests)
set_target_properties(security_ocr_window_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME ui.security_ocr COMMAND security_ocr_window_tests)
set_tests_properties(ui.security_ocr PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

qt_add_executable(redaction_tests RedactionTest.cpp)
target_link_libraries(redaction_tests PRIVATE pdf_editor_core Qt6::Test)
pdf_editor_enable_warnings(redaction_tests)
set_target_properties(redaction_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME core.redaction COMMAND redaction_tests)
set_tests_properties(core.redaction PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

qt_add_executable(redaction_window_tests RedactionWindowTest.cpp)
target_link_libraries(redaction_window_tests PRIVATE pdf_editor_ui Qt6::Test)
pdf_editor_enable_warnings(redaction_window_tests)
set_target_properties(redaction_window_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME ui.redaction COMMAND redaction_window_tests)
set_tests_properties(ui.redaction PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

qt_add_executable(content_signature_tests ContentSignatureTest.cpp)
target_link_libraries(content_signature_tests PRIVATE pdf_editor_core Qt6::Test Pdf4QtLibCore)
add_custom_command(TARGET content_signature_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/VerifyDetachedSignature.ps1" "$<TARGET_FILE_DIR:content_signature_tests>/VerifyDetachedSignature.ps1")
pdf_editor_enable_warnings(content_signature_tests)
set_target_properties(content_signature_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME core.content_signature COMMAND content_signature_tests)
set_tests_properties(core.content_signature PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

qt_add_executable(content_signature_window_tests ContentSignatureWindowTest.cpp)
target_link_libraries(content_signature_window_tests PRIVATE pdf_editor_ui Qt6::Test Pdf4QtLibCore)
pdf_editor_enable_warnings(content_signature_window_tests)
set_target_properties(content_signature_window_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME ui.content_signature COMMAND content_signature_window_tests)
set_tests_properties(ui.content_signature PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

qt_add_executable(remaining_features_tests RemainingFeaturesTest.cpp)
target_link_libraries(remaining_features_tests PRIVATE pdf_editor_core Qt6::Test)
pdf_editor_enable_warnings(remaining_features_tests)
set_target_properties(remaining_features_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME core.remaining_features COMMAND remaining_features_tests)
set_tests_properties(core.remaining_features PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

qt_add_executable(remaining_features_window_tests RemainingFeaturesWindowTest.cpp)
target_link_libraries(remaining_features_window_tests PRIVATE pdf_editor_ui Qt6::Test)
pdf_editor_enable_warnings(remaining_features_window_tests)
set_target_properties(remaining_features_window_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME ui.remaining_features COMMAND remaining_features_window_tests)
set_tests_properties(ui.remaining_features PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
