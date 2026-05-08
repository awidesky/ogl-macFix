// Include standard headers
#include <stdio.h>
#include <stdlib.h>
#include <utility>
#include <vector>


// Include GLEW
#include <GL/glew.h>

// Include GLFW
#include <GLFW/glfw3.h>
GLFWwindow *window;

// Include GLM
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/norm.hpp>
using namespace glm;

#include <common/shader.hpp>
#include <common/objloader.hpp>
#include <common/texture.hpp>

// 키보드와 마우스 입력 처리
static void processKeyboardMouseInput(glm::mat4& mat, glm::vec3& tractorPosition);
//opengl 초기화
static int glinit();

// 에러 체크
void checkGLerror(const char*);

// 디폴트 텍스쳐는 오브젝트가 단색이 아닌 텍스쳐를 쓰기로 되어 있으나(colorcheck < 0),
// 텍스쳐가 주어져 있지 않은 경우 쓴다.
// 일종의 오류 상황이므로, 주로 눈에 잘 띄는 색을 쓰는 것이 좋다.
// 여기서는 Valve의 "missing textures checkerbord"를 사용했다.
static GLuint defaultTexture;

// shader uniform 요소
// TextureUniformID는 sampler2D 객체이며, colorCheck값에 따라 텍스쳐를 쓸지, 아니면 단색으로 표시할지 정해진다.
// colorCheck 가 -1인 경우 정해진 택스쳐를 쓰며, 다른 값일 경우 각각 정해진 단색을 쓴다.
static GLuint TextureUniformID, ColorCheckUniformID;
// MVP 각 벡터에 대한 uniform. 빛 계산을 위해 따로 셰이더에 가져가야 한다.
static GLuint ModelUniformID, ViewUniformID, ProjectionUniformID;
// 광원의 속성
static GLuint LightPosUniformID, ViewPosUniformID, LightColorUniformID;

// MVP 중 VP행렬
static glm::mat4 Projection;
static glm::mat4 View;

// 원래 torus.obj 모델은 너무 크므로, 1/10으로 줄여 쓴다
const float TORUS_BASE_SCALE = 0.1f;

/*
* 모델(obj파일)을 나타내는 객체.
* obj파일에서 읽어온 버텍스 값과 gl buffer를 저장한다.
*/
struct modelData {
    std::vector<glm::vec3> vertices;
    GLuint vertexbuffer, uvbuffer, normalbuffer;
    modelData(const char * path) {
        std::vector<glm::vec2> uvs;
        std::vector<glm::vec3> normals;
        loadOBJ(path, vertices, uvs, normals);
        glGenBuffers(1, &vertexbuffer);
        glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), &vertices[0], GL_STATIC_DRAW);
        
        glGenBuffers(1, &uvbuffer);
        glBindBuffer(GL_ARRAY_BUFFER, uvbuffer);
        glBufferData(GL_ARRAY_BUFFER,uvs.size() * sizeof(glm::vec2), &uvs[0], GL_STATIC_DRAW);

        glGenBuffers(1, &normalbuffer);
        glBindBuffer(GL_ARRAY_BUFFER, normalbuffer);
        glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(glm::vec3), &normals[0], GL_STATIC_DRAW);

    }
    ~modelData() {
        glDeleteBuffers(1, &vertexbuffer);
        glDeleteBuffers(1, &uvbuffer);
        glDeleteBuffers(1, &normalbuffer);
    }
};

/*
* 모델로 만든 오브젝트를 나타낸다.
* 모델의 기본 위치, 모델, 텍스쳐, colorcheck등 속성을 가진다.
*/
struct object {
    glm::vec3 position;
    glm::mat4 modelMatrix;
    modelData model;
    GLuint texture;
    int colorCheck;
    
    object(const modelData& m, int colorCheck, float x, float y, float z) : object(m, colorCheck, vec3(x, y, z)) {}
    object(const modelData& m, int colorCheck, vec3&& pos)
        : model(m), colorCheck(colorCheck), position(pos), modelMatrix(1.0f), texture(defaultTexture) {}

    // 물체를 변환하는 함수들, modelMatrix를 변환한다.
    void Translate(float x, float y, float z) {
        Translate(vec3(x, y, z));
    }
    void Translate(const glm::vec3& move) {
        modelMatrix = translate(modelMatrix, move);
    }
    void MoveToPosition() {
        Translate(position);
    }
    void Rotate(float degree, vec3 axis) { //local rotate
        modelMatrix = rotate(modelMatrix, glm::radians(degree), axis);
    }
    void Rotate(float yaw, float pitch, float roll) { //local rotate
        modelMatrix = modelMatrix * eulerAngleYXZ(yaw, pitch, roll);
    }
    void Scale(float x, float y, float z) {
        Scale(vec3(x, y, z));
    }
    void Scale(vec3&& v) {
        modelMatrix = scale(modelMatrix, v);
    }
    // 그리는 작업을 진행하는 함수.
    void draw() {
        // MVP에 대한 행렬들 전송
        glUniformMatrix4fv(ModelUniformID, 1, GL_FALSE, &modelMatrix[0][0]);
        glUniformMatrix4fv(ViewUniformID, 1, GL_FALSE, &View[0][0]);
        glUniformMatrix4fv(ProjectionUniformID, 1, GL_FALSE, &Projection[0][0]);

        // colorCheck값에 따라 색칠하는 방식 바뀐다.
        glUniform1i(ColorCheckUniformID, colorCheck);
        
        // 단색을 쓰더라도 텍스쳐 매퍼는 사용된다, 코드의 복잡성을 감소시키기 위해...
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(TextureUniformID, 0);

        // 1rst attribute buffer : vertices
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, model.vertexbuffer);
        glVertexAttribPointer(
            0,          // attribute. No particular reason for 0, but must match the layout in the shader.
            3,          // size
            GL_FLOAT, // type
            GL_FALSE, // normalized?
            0,          // stride
            (void *)0 // array buffer offset
        );
        
        // uv
        glEnableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, model.uvbuffer);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);

        // normal
        glEnableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, model.normalbuffer);
        glVertexAttribPointer(
            2,              // layout(location = 2)
            3,
            GL_FLOAT,
            GL_FALSE,
            0,
            (void*)0
        );


        // Draw the model !
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)model.vertices.size());
        
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);

    }
};

/*
* 회전하는 핸들을 위한 클래스.
* 객체의 현재 회전 각도와, 애니메이션이 끝까지 진행되었을 때 몇 도 돌아야 하는지를 저장한다.
*/
struct rotateObject : public object {
    float localAngle, animationRotate;
    rotateObject(const modelData& m, int colorCheck, vec3&& pos, float localAngle, float animationRotate)
        : object(m, colorCheck, std::move(pos)), localAngle(localAngle), animationRotate(animationRotate) {}

};

// 애니메이션 시작 시간. 음수인 경우 애니메이션이 시작되지 않은 것.
double animationStartTime = -1;
// 이전 프레임 시간. 애니메이션 시간 동안만 사용된다.
double lastFrameTime = -1;
// 땅 파는 모션은 삽이 땅을 팠다가 다시 올라가는 과정으로 이루어진다.
// 즉, keyframe초 동안 핸들이 내려가 버켓으로 땅을 팠다가,
// 다시 keyframe초 동안 핸들이 올라간다.
// keyframe 값을 조정하면 애니메이션의 빠르기를 정할 수 있다.
const float keyframe = 3.0f;
// 3인칭 뷰인가?
bool thirdView = true;

void start()
{
    //모델 객체를 만든다.
    modelData cube{ "models/cube.obj" };
    modelData plane{ "models/plane.obj" };
    modelData torus{ "models/torus.obj" };

    // VAO
    GLuint VertexArrayID;
    glGenVertexArrays(1, &VertexArrayID);
    glBindVertexArray(VertexArrayID);

    // Create and compile our GLSL program from the shaders
    GLuint programID = LoadShaders("TransformVertexShader.vertexshader", "ColorFragmentShader.fragmentshader");

    // 각 오브젝트에 맞는 텍스쳐를 가져온다.
    defaultTexture = loadBMP_custom("textures/default.bmp");
    GLuint groundTexture = loadBMP_custom("textures/grid.bmp");
    GLuint trackTexture = loadBMP_custom("textures/track.bmp");
    GLuint scoopTexture = loadBMP_custom("textures/scoop.bmp");
    GLuint cabinTexture = loadBMP_custom("textures/cabin.bmp");
    GLuint bodyTexture = loadBMP_custom("textures/body.bmp");

    // 모든 유니폼 변수 ID를 가져온다
    TextureUniformID = glGetUniformLocation(programID, "myTextureSampler");
    ModelUniformID = glGetUniformLocation(programID, "model");
    ViewUniformID = glGetUniformLocation(programID, "view");
    ProjectionUniformID = glGetUniformLocation(programID, "projection");
    ColorCheckUniformID = glGetUniformLocation(programID, "colorCheck");
    LightPosUniformID = glGetUniformLocation(programID, "lightPos");
    ViewPosUniformID = glGetUniformLocation(programID, "viewPos");
    LightColorUniformID = glGetUniformLocation(programID, "lightColor");


    // Projection matrix : 45 Field of View, 4:3 ratio, display range : 0.01 unit <-> 500 units
    Projection = glm::perspective(45.0f, 4.0f / 3.0f, 0.01f, 500.0f);

    // View matrix, 기본적으로 thirdView에서 시작한다.
    View = glm::lookAt(
        glm::vec3(4, 4, 4),    // Camera position in World Space
        glm::vec3(0, 0, 0),    // and looks at the origin
        glm::vec3(0, 1, 0)     // Head is up (set to 0,-1,0 to look upside-down)
    );
    glm::mat4 ThirdView = View;
    // 포크레인의 시작 위치, WASD(위, 왼쪽, 아래, 오른쪽)과 Q(뒤쪽), E(앞쪽)을 누르면 바뀐다.
    glm::vec3 tractorPosition = vec3(0);

    // ---------------오브젝트 선언 및 초기화---------------//
    // 땅바닥을 표현, 움직임 확인과 축 확인에 용이하다.
    object ground{ plane, -1, vec3(0, 0, 0) }; // 평면 모델을 쓰고, 색칠은 텍스쳐를 이용하며, 원점에 존재한다.
    ground.texture = groundTexture; // Z, X가 표시된 텍스쳐를 씌운다.
    ground.MoveToPosition();
    ground.Scale(10.0f, 1.0f, 10.0f);
    ground.Rotate(0.0f, 0.0f, 3.14159f); // 눕힌다.

    // 트랙터 밑 부분 차체
    object body{ cube, -1, 0.0f, 2.0f, 0.0f }; //위로 살짝 떠 있다.
    body.texture = bodyTexture; // 알맞은 텍스쳐 가져온다.

    // 운전수가 타는 위쪽 부분.
    object cabin{ cube, -1, 0.0f, 3.2f, 0.0f };
    cabin.texture = cabinTexture;

    // 무한궤도를 토러스로 표현했다. 두개가 한 쌍이므로 배열로 묶었다.
    object track[] = {
        { torus, -1, +10.0f, 6.0f, 0.0f },
        { torus, -1, -10.0f, 6.0f, 0.0f }
    };
    track[0].texture = track[1].texture = trackTexture;

    // 핸들과 버켓.
    // 맨 뒤쪽 두 파라메터는 localAngle과 animationRotate이다.
    //처음에는 X축 기준 localAngle도 기울어져 있고, 애니메이션 시작 시 총 animationRotate도 기울어지다 다시 돌아온다.
    rotateObject arm1{ cube, 2, vec3(0.0f, 1.7f, 1.9f), -60.0f, 20.0f }; // 핸들은 단색을 사용한다.
    rotateObject arm2{ cube, 1, vec3(0.0f, 1.7f, 3.9f), 40.0f, 20.0f };
    rotateObject arm3{ cube, 3, vec3(0.0f, 1.7f, 5.8f), 100.0f, -50.0f };
    // 버켓은 도넛 모양으로 
    rotateObject bucket{ torus, -1, vec3(0.0f, 1.9f, 7.5f), 10.0f, 30.0f };
    // 도넛은 뒤가 뚫려 있으므로, plane을 하나 써서 뒤를 막아 준다.
    rotateObject bucketBackPlane{ plane, -1, vec3(0.0f, 2.2f, 7.5f), 10.0f, 0.0f }; // 토러스에 종속되므로 애니메이션 간 회전하지 않는다.
    // 사실상 같은 물체를 구성하므로, 텍스쳐를 같이 쓴다.
    bucket.texture = bucketBackPlane.texture = scoopTexture;

    glDeTexture(9999, 9999);

    // ---------------렌더링 루프---------------//
    do
    {
        checkGLerror("Loop start point"); // 에러가 있는지 체크한다.

        // ---------------애니메이션 처리---------------//
        if (animationStartTime > 0.0) { //애니메이션이 시작했다면..
            double currentTime = glfwGetTime();
            // 프레임 간 시간 차이
            double deltaTime = currentTime - lastFrameTime;
            lastFrameTime = currentTime;

            // 애니메이션 시작으로부터 몇 초 지났는가?
            float timeInterval = currentTime - animationStartTime;

            // 애니메이션 시작 후 keyframe초 동안 총 animationRotate도 돌아간다.
            if (timeInterval < keyframe) {
                arm1.localAngle += (arm1.animationRotate / keyframe) * deltaTime; // 초당 각도 * 프레임 동안의 시간
                arm2.localAngle += (arm2.animationRotate / keyframe) * deltaTime;
                arm3.localAngle += (arm3.animationRotate / keyframe) * deltaTime;
                bucket.localAngle += (bucket.animationRotate / keyframe) * deltaTime;
                bucketBackPlane.localAngle += (bucketBackPlane.animationRotate / keyframe) * deltaTime;
            }
            // 이후 다시 keyframe초 동안 총 -animationRotate도 돌아간다.
            else if (timeInterval >= keyframe && timeInterval < 2 * keyframe) {
                arm1.localAngle -= (arm1.animationRotate / keyframe) * deltaTime;
                arm2.localAngle -= (arm2.animationRotate / keyframe) * deltaTime;
                arm3.localAngle -= (arm3.animationRotate / keyframe) * deltaTime;
                bucket.localAngle -= (bucket.animationRotate / keyframe) * deltaTime;
                bucketBackPlane.localAngle -= (bucketBackPlane.animationRotate / keyframe) * deltaTime;
            }
            else {
                //회전이 모두 완료되었다면, 두 값을 음수로 설정해 애니메이션이 끝났음을 나타낸다.
                animationStartTime = lastFrameTime = -1;
            }

        }

        // ---------------모델 변환---------------//
        //트랙터 오브젝트를 위한 Model matrix의 초깃값. tractorPosition만큼 이동한다.
        glm::mat4 world = translate(mat4(1.0f), tractorPosition);

        // 크기에 맞게 스케일 후, 이동한다.
        body.modelMatrix = world;
        body.Scale(0.7, 0.6, 1.0);
        body.MoveToPosition();

        cabin.modelMatrix = world;
        cabin.Scale(0.4, 0.8, 0.6);
        cabin.MoveToPosition();

        track[0].modelMatrix = track[1].modelMatrix = world;
        //토러스 모델이 매우 크므로 작게 줄인다
        track[0].Scale(TORUS_BASE_SCALE, TORUS_BASE_SCALE, TORUS_BASE_SCALE);
        track[1].Scale(TORUS_BASE_SCALE, TORUS_BASE_SCALE, TORUS_BASE_SCALE);
        track[0].MoveToPosition(); track[1].MoveToPosition();
        // 토러스를 세운다.
        track[0].Rotate(90.0f, glm::vec3(0, 0, 1));
        track[1].Rotate(90.0f, glm::vec3(0, 0, 1));
        // 찌그러뜨리기
        track[0].Scale(
            0.32f, // 찌그러짐
            0.8f, // 무한궤도 너비
            0.9f   // 무한궤도 길이
        );
        track[1].Scale(0.32f, 0.8f, 0.9f);
        // Q, E키를 누르면 트랙터가 앞으로 간다, 이에 맞춰서 바퀴도 굴려 주면,
        // 텍스쳐가 줄무늬 모양이므로 무한궤도가 돌아가는 것처럼 보이게 된다.
        track[0].Rotate(fmod(tractorPosition.z, 10.0) * 36, glm::vec3(0, -1, 0));
        track[1].Rotate(fmod(tractorPosition.z, 10.0) * 36, glm::vec3(0, -1, 0));


        // 월드 기준 (tractorPosition)
        arm1.modelMatrix = world;
        // arm1이 body에 붙는 위치로 이동
        arm1.MoveToPosition();
        // pivot, 회전, 복귀
        // 왼쪽 끝을 중심으로 회전하는 것처럼 보이기 위해, 이동 후 회전하고 다시 돌아온다.
        // 다시 돌아오는 건 스케일링 필요하기 때문 + 연결된 물체들이 이 오브젝트의 Model Matrix를 상속받기 때문이다.
        arm1.Translate(0, 0, -2.0f * 0.5f);
        arm1.Rotate(arm1.localAngle, vec3(1, 0, 0));
        arm1.Translate(0, 0, 2.0f * 0.5f);

        arm2.modelMatrix = arm1.modelMatrix;
        // arm1 말단 -> arm2 시작 위치로 이동
        arm2.Translate(arm2.position - arm1.position);
        // pivot, 회전, 복귀
        arm2.Translate(0, 0, -2.0f * 0.5f);
        arm2.Rotate(arm2.localAngle, vec3(1, 0, 0));
        arm2.Translate(0, 0, 2.0f * 0.5f);

        arm3.modelMatrix = arm2.modelMatrix;
        arm3.Translate(arm3.position - arm2.position);
        arm3.Translate(0, 0, -2.0f * 0.5f);
        arm3.Rotate(arm3.localAngle, vec3(1, 0, 0));
        arm3.Translate(0, 0, 2.0f * 0.5f);

        bucket.modelMatrix = arm3.modelMatrix;
        bucket.Translate(bucket.position - arm3.position);
        vec3 torusTopPivot = vec3(0.0f, 2.0f, 0.0f) * TORUS_BASE_SCALE;
        bucket.Translate(-torusTopPivot);
        bucket.Rotate(bucket.localAngle, vec3(1, 0, 0));
        bucket.Translate(torusTopPivot);

        bucketBackPlane.modelMatrix = bucket.modelMatrix;
        bucketBackPlane.Translate(bucketBackPlane.position - bucket.position);
        bucketBackPlane.Translate(0.0f, -0.5f, 0.0f);
        bucketBackPlane.Rotate(bucketBackPlane.localAngle, vec3(1, 0, 0));
        bucketBackPlane.Translate(0.0f, 0.5f, 0.0f);

        // 이동과 회전 완료 후에 알맞게 크기를 바꾼다.
        arm1.Scale(0.15f, 0.15f, 1.1f);
        arm2.Scale(0.14f, 0.14f, 1.0f);
        arm3.Scale(0.12f, 0.12f, 1.1f);
        bucket.Scale(vec3(0.4f, 0.8f, 0.4f) * TORUS_BASE_SCALE);
        bucketBackPlane.Scale(vec3(0.5f));


        // ---------------렌더링---------------//
        // Clear the screen
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Get mouse & keyboard input
        processKeyboardMouseInput(ThirdView, tractorPosition);

        // 스페이스를 눌렀다면, 다음 프레임을 위해 View를 바꿔야 한다.
        if (!thirdView) { // next view
            glm::mat4 cabinWorld = cabin.modelMatrix;
            // cabin, 즉 운전석의 앞쪽 면을 카메라 좌표로 한다.
            vec3 cpos = vec3(cabinWorld[3]) + vec3(0.0f, 0.0f, 0.61f); // 너비가 0.6이므로, 그보다 살짝 앞
            View = glm::lookAt(
                cpos,
                cpos + vec3(0.0f, 0.0f, 1.0f), // Z축 방향을 본다. 문제에서는 트랙터의 회전을 요구하지 않았으므로...
                vec3(0, 1, 0)
            );
        }
        else {
            View = ThirdView;
        }

        // Use our shader
        glUseProgram(programID);


        // 트랙터의 위치를 뽑아 정확히 앞에서 오도록 계산할 수도 있지만,
        // 이렇게 두면 트랙터를 움직이며 specular를 관찰하기 편하다.
        glm::vec3 lightPos = glm::vec3(0.0f, 0.0f, 100.0f);
        // 따뜻한 노을빛. 백색광보다는 분위기 있어 보인다.
        glm::vec3 lightColor = glm::vec3(0.933, 0.686, 0.380);
        // 카메라 위치 (View 행렬에서 추출)
        glm::vec3 viewPos = glm::vec3(glm::inverse(View)[3]);
        // lighting을 위한 값을 셰이더에 전송한다
        glUniform3fv(LightPosUniformID, 1, &lightPos[0]);
        glUniform3fv(ViewPosUniformID, 1, &viewPos[0]);
        glUniform3fv(LightColorUniformID, 1, &lightColor[0]);


        // 모든 물체를 그린다.
        ground.draw();
        body.draw();
        cabin.draw();
        track[0].draw(); track[1].draw();
        arm1.draw(); arm2.draw(); arm3.draw();
        bucket.draw(); bucketBackPlane.draw();

        // Swap buffers
        glfwSwapBuffers(window);
        glfwPollEvents();

    } // Check if the ESC key was pressed or the window was closed
    while (glfwGetKey(window, GLFW_KEY_ESCAPE) != GLFW_PRESS &&
        glfwWindowShouldClose(window) == 0);

    // Cleanup VBO and shader
    glDeleteProgram(programID);
    glDeleteVertexArrays(1, &VertexArrayID);

    // model객체의 소멸자가 호출되어, vertexbuffer, uvbuffer, normalbuffer도 삭제된다.
    // (start와 main을 분리한 이유)
}

int main(void)
{
    // 초기화 코드가 길어, 따로 분리했다.
    int ret = glinit();
    if (ret) return ret;
    
    start();
    // 모든 opengl 데이터가 cleanup된 것 확인

    // Close OpenGL window and terminate GLFW
    glfwTerminate();

    return 0;
}

/* static variable for Keyboard & Mouse input processing */
static double lastTime = 0.0;
static float speed = 3.0f, mouseSpeed = 10.0f; // units / second
static double xpos = 0.0, ypos = 0.0;
static bool firstPress = true;
static double xpos_prev = 0.0, ypos_prev = 0.0;

/*
* 마우스의 움직임을 계산하고, 뷰 행렬을 회전한다. 
* 환경에 따라 속도가 다르거나 시점 회전이 직관적이지 않아,
* 화살표 키를 통한 시점 회전을 따로 제작했다.
*/
static void computeMouseRotates(glm::mat4& mat) {
    // Initial horizontal angle : toward -Z
    float horizontalAngle = 0.0f;
    // Initial vertical angle : none
    static float verticalAngle = 0.0f;

    // Compute time difference between current and last frame
    double currentTime = glfwGetTime();
    float deltaTime = float(currentTime - lastTime);

    // Get mouse position
    glfwGetCursorPos(window, &xpos, &ypos);

    // Compute new orientation
    if (xpos < xpos_prev)
        horizontalAngle = -deltaTime * mouseSpeed;
    else if (xpos > xpos_prev)
        horizontalAngle = deltaTime * mouseSpeed;
    else
        horizontalAngle = 0.0;

    if (ypos < ypos_prev)
        verticalAngle = -deltaTime * mouseSpeed;
    else if (ypos > ypos_prev)
        verticalAngle = deltaTime * mouseSpeed;
    else
        verticalAngle = 0.0;

    mat *= glm::eulerAngleYXZ(horizontalAngle, verticalAngle, 0.0f);

    xpos_prev = xpos;
    ypos_prev = ypos;

    // For the next frame, the "last time" will be "now"
    lastTime = currentTime;
}

static bool spacePrev = false;

static void computeKeyboardTranslates(glm::mat4& view, glm::vec3& tractorPosition)
{
    // 1번을 누르면 WireFrame형태로 보여지며, 2번을 누르면 다시 돌아온다.
    // 모델이 파뭍히거나 가려져 안 보이는 경우 디버깅을 용이하게 한다.
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    
    // 엔터 키를 눌렀고, 애니메이션이 이미 시작하지 않았고, thirdView라면
    // 애니메이션 타임을 리셋, 즉 0으로 한다.
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) {
        if(animationStartTime < 0.0 && thirdView) lastFrameTime = animationStartTime = glfwGetTime();
    }

    // 스페이스를 누른 경우 시점을 변환한다. 애니메이션이 시작하지 않았어야 한다.
    bool spaceNow = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (spaceNow && !spacePrev) {
        if (animationStartTime < 0.0) thirdView = !thirdView;
    }
    spacePrev = spaceNow;


    // Compute time difference between current and last frame
    double currentTime = glfwGetTime();
    float deltaTime = float(currentTime - lastTime);


    glm::vec3 modelMove(0.0f);
    // 굴삭기 이동
    float modelSpeed = 2.0f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        modelMove += glm::vec3(0.0f, 1.0f, 0.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        modelMove += glm::vec3(0.0f, -1.0f, 0.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        modelMove += glm::vec3(-1.0f, 0.0f, 0.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        modelMove += glm::vec3(1.0f, 0.0f, 0.0f);
    }
    // 앞뒤로 이동하는 추가 기능
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        modelMove += glm::vec3(0.0f, 0.0f, -1.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        modelMove += glm::vec3(0.0f, 0.0f, 1.0f);
    }

    // 방향벡터로 정규화 + 속도 적용
    if (glm::length(modelMove) > 0.0f) {
        modelMove = glm::normalize(modelMove);
        tractorPosition += modelMove * modelSpeed * deltaTime;
    }
    

    if (thirdView) {
        // 화살표 키 입력에 따른 시선 회전. 방향키 방향에 맞게 움직어 조금 더 직관적이다.
        float horizontalAngle = 0.0f, verticalAngle = 0.0f, rotationSpeed = 1.0f;
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
            horizontalAngle -= rotationSpeed * deltaTime;
        }
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            horizontalAngle += rotationSpeed * deltaTime;
        }
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
            verticalAngle += rotationSpeed * deltaTime;
        }
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            verticalAngle -= rotationSpeed * deltaTime;
        }
        view = glm::eulerAngleYXZ(horizontalAngle, -verticalAngle, 0.0f) * view;


        // 카메라의 방향 벡터 추출
        glm::mat4 cameraWorldMatrix = glm::inverse(view);
        glm::vec3 forward = glm::normalize(glm::vec3(cameraWorldMatrix[2])); // 카메라의 시선 방향
        glm::vec3 right = -glm::normalize(glm::vec3(cameraWorldMatrix[0]));   // 카메라의 오른쪽 방향
        glm::vec3 up = -glm::normalize(glm::vec3(cameraWorldMatrix[1]));      // 카메라의 위쪽 방향

        glm::vec3 translateFactor = glm::vec3(0.0f);

        // 카메라 자체를 이동하는 추가 기능.
        // IJKL로 사방으로 이동할 수 있으며, O는 위로, U는 아래로 이동한다
        if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) {
            translateFactor += forward * deltaTime * speed;
        }
        if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) {
            translateFactor -= forward * deltaTime * speed;
        }
        if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) {
            translateFactor += right * deltaTime * speed;
        }
        if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) {
            translateFactor -= right * deltaTime * speed;
        }
        if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) {
            translateFactor += up * deltaTime * speed;
        }
        if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS) {
            translateFactor -= up * deltaTime * speed;
        }

        view *= glm::translate(glm::mat4(1.0f), translateFactor);
    }
    // For the next frame, the "last time" will be "now"
    lastTime = currentTime;
}

void checkGLerror(const char* msg) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        fprintf(stderr, "GL ERROR %d : %s\n", err, msg);
    }
}

static void processKeyboardMouseInput(glm::mat4& view, glm::vec3& tractorPosition)
{
    // Compute the Model matrix from keyboard and mouse input
    if (thirdView && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
    { // 삼인칭 뷰인 경우에만 마우스 입력 활성화
        if (firstPress)
        {
            glfwGetCursorPos(window, &xpos_prev, &ypos_prev);
            firstPress = false;
        }

        computeMouseRotates(view);
    }

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_RELEASE)
        firstPress = true;

    computeKeyboardTranslates(view, tractorPosition);
}

// opengl 라이브러리 초기화 작업
static int glinit()
{

    // Initialise GLFW
    if (!glfwInit())
    {
        fprintf(stderr, "Failed to initialize GLFW\n");
        (void)getchar();
        return -1;
    }

    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // To make MacOS happy; should not be needed
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Open a window and create its OpenGL context
    window = glfwCreateWindow(1024, 768, u8"12211723 홍성민 - 굴삭기 시뮬레이터", NULL, NULL);
    if (window == NULL)
    {
        fprintf(stderr, "Failed to open GLFW window. If you have an Intel GPU, they are not 3.3 compatible. Try the 2.1 version of the tutorials.\n");
        (void)getchar();
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // Initialize GLEW
    glewExperimental = true; // Needed for core profile
    if (glewInit() != GLEW_OK)
    {
        fprintf(stderr, "Failed to initialize GLEW\n");
        (void)getchar();
        glfwTerminate();
        return -1;
    }
    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));

    glGetError(); // 처음에 1280 에러 발생하긴 하지만, 별로 중요한 오류 같진 않음
    checkGLerror("GL init");

    // Ensure we can capture the escape key being pressed below
    glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);

    // Dark blue background
    glClearColor(0.0f, 0.0f, 0.4f, 0.0f);

    // Enable depth test
    glEnable(GL_DEPTH_TEST);
    // Accept fragment if it closer to the camera than the former one
    glDepthFunc(GL_LESS);
    
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    
    return 0;
}
