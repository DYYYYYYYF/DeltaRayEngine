import os           # File operatorations
import platform     # Cross platform
import sys, getopt  # Command line argvs
import json

# Common values
vulkanPath       = "D:/C_Library/VulkanSDK"
compile_command  = "glslc -c "
file_path        = os.path.abspath(".") + "/Shaders/"
target_path      = os.path.abspath(".") + "/Assets/Shaders/"

# compile failed files
failed_shaders = []
glsl_list = [".vert", ".frag", ".geom", ".tesc", ".tese", ".comp"]

def CheckGlslc():
    # global value will be changed, so we need define the value inside func
    global compile_command

    compile_check_command     = compile_command + "--version"
    if os.system(compile_check_command) != 0:
        compile_check_command = file_path + "tools/glslc.exe -c "
        
        if os.system(compile_check_command) != 0:
            # Not set global command
            os.system("cls")
            compile_command = vulkanPath + "/Bin/glslc.exe -c "
            compile_check_command = compile_command + "--version "
            
            if os.system(compile_check_command) != 0:
                # Not set right vulkan path
                os.system("cls")
                print("Please set vulkan path!")
                quit()


def Compile(sub_file_path, file, filename, shader_language):
    shader_type = filename[1].split('.')[1]
    if filename[1] in glsl_list and shader_language == "glsl":
        ## Record compile command
        cmd = compile_command + sub_file_path + "/" + file + " -o " + target_path + filename[0] + "." + shader_type + ".spv"
        print("Compiling " + sub_file_path + "/" + filename[0] + filename[1])

        ## Execute compile command
        if os.system(cmd) != 0:
            failed_shaders.append(file)

    elif filename[1] == ".hlsl" and shader_language == "hlsl":
        sub_filename = os.path.splitext(filename[0])
        shader_type = sub_filename[1].split('.')[1]

        ## Record compile command
        cmd = compile_command + " --target-env=vulkan1.3 -fpreserve-bindings -c -fshader-stage=" + shader_type \
            + " -fentry-point=main " + sub_file_path + "/" + file + " -o " + target_path + filename[0] + ".spv"
        print("Compiling " + sub_file_path + "/" + filename[0] + filename[1])

        ## Execute compile command
        if os.system(cmd) != 0:
            failed_shaders.append(file)


def SearchShaderFiles(sub_file_path, shader_language):
    for root, dirs, files in os.walk(sub_file_path):
        for dir in dirs:
            SearchShaderFiles(dir, shader_language)
        for file in files:
            filename = os.path.splitext(file)
            if filename[1] != ".spv" and filename[1] != ".exe" and filename[1] != '':
                Compile(root, file, filename, shader_language)


def CompileShaders(shader_language):
    global failed_shaders
    print('Compile shader language: ' + shader_language)

    # Check target path
    if not os.path.exists(target_path):
        print(target_path + " is not exists. mkdir...")
        os.mkdir(target_path)


    # Windows platform
    # Check glslc command
    if platform.system().lower() == "windows":
        CheckGlslc()

    print("Shader direction: " + file_path + "\n")
    SearchShaderFiles(file_path, shader_language)

    LogCompileResult()
    

def LogCompileResult():
    if len(failed_shaders) == 0:
        print("\nAll shaders compiled successful...")
        print("The spv files directories: " + target_path)
    else:
        print("\nComile failed shaders:")
        for fail_hader in failed_shaders:
            print(fail_hader)


if __name__ == '__main__':
    try:
        opts, args = getopt.getopt(sys.argv[1:], "hl:", ["help", "language="])
        # 默认与引擎运行时保持一致（Engine/Config.json 的 renderer.shader_language 当前为 hlsl）
        default_shader_language = "hlsl"
        for opt, arg in opts:
            if opt in ("-h", "--help"):
                print('-l [glsl/hlsl]: shader language')
                sys.exit()
            elif opt in ("-l", "--language"):
                default_shader_language = arg

        CompileShaders(default_shader_language)

        # 保存JSON：引擎读取的是 Engine/Config.json 中嵌套的 renderer.shader_language
        # （见 Engine/Systems/ShaderSystem.cpp 初始化时的 ReadString("renderer.shader_language")），
        # 因此必须写回该嵌套位置，保持其他字段与结构不变。
        ConfigPath = os.path.abspath(".")+'/Engine/Config.json'
        with open(ConfigPath, 'r', encoding='utf-8') as ConfigFile:
            data = json.load(ConfigFile)

        if not isinstance(data.get('renderer'), dict):
            data['renderer'] = {}
        data['renderer']['shader_language'] = default_shader_language

        with open(ConfigPath, 'w', encoding='utf-8') as ConfigFile:
            json.dump(data, ConfigFile, indent=2, ensure_ascii=False)

    except getopt.GetoptError:
        print('compile_shader.py -l <language>')
