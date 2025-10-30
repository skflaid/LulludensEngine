#pragma once

class ISystem {
public:
    virtual ~ISystem() = default;

    virtual void Initialize() = 0;
    virtual void Update(float deltaTime) = 0;
    virtual void Shutdown() = 0;

    virtual const char* GetName() const = 0;

    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }

protected:
    bool m_Enabled = true;
};
